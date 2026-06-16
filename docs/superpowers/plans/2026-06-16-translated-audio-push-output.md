# Translated Audio Push-Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the translated-audio output path so the client pushes every received PCM byte to OBS as it arrives, letting the OBS mixer be the clock — eliminating sentence-tail truncation and the 1200 ms startup latency.

**Architecture:** The network thread decodes Gemini audio into a bounded ring buffer and notifies a condition variable. The audio source's own thread wakes, drains all available audio, and hands it to `obs_source_output_audio` with contiguous, duration-spaced timestamps produced by a pure `OutputTimestamper`. Turn-boundary signals (`turnComplete`/`generationComplete`/`interrupted`) are parsed; `interrupted` drops stale audio. The fixed 20 ms emit loop and `OutputJitterBuffer` state machine are deleted.

**Tech Stack:** C++17, CMake (obs-plugintemplate), libobs (OBS 31.x), IXWebSocket, nlohmann/json, Catch2. Windows x64.

**Spec:** `docs/superpowers/specs/2026-06-16-translated-audio-push-output-design.md`

---

## Build & test commands (Windows)

Configure once per checkout (downloads OBS deps via `buildspec.json`):

```powershell
cmake --preset windows-x64
```

- Build everything (the plugin DLL + tests): `cmake --build --preset windows-x64`
- Build only the libobs-free unit tests: `cmake --build --preset windows-x64 --target unit-tests`
- Run all unit tests: `ctest --test-dir build_x64 --output-on-failure`
- Run one unit test by name: `ctest --test-dir build_x64 -R "<test case name>" --output-on-failure`

Tasks 1, 3, and 5 are verifiable with the `unit-tests` target alone. Tasks 2, 4, and 6 require a full plugin build (libobs). If a full plugin build cannot run because libobs is unavailable, stop and report that, rather than marking the task complete.

---

## File structure

- `src/live-protocol.hpp` / `.cpp` — add turn-boundary flags to `ServerMessage`; parse them. (Task 1)
- `src/translation-session.hpp` / `.cpp` — output-ready notification, interrupt signalling, and the source-facing wait/read API; wire the WebSocket callback to the new flags. (Task 2, with cleanup in Task 4)
- `src/audio-pacing.hpp` / `.cpp` — add the pure `OutputTimestamper` (Task 3); delete the dead `OutputJitterBuffer` / `PcmS16MonoSmoother` afterwards (Task 5).
- `src/source.cpp` — replace the fixed-clock emit loop with the event-driven push loop. (Task 4)
- `tests/test-live-protocol.cpp`, `tests/test-audio-pacing.cpp` — unit tests for the pure logic.

No `CMakeLists.txt` changes are required.

---

## Task 1: Parse Gemini turn-boundary signals

**Files:**
- Modify: `src/live-protocol.hpp`
- Modify: `src/live-protocol.cpp`
- Test: `tests/test-live-protocol.cpp`

- [ ] **Step 1: Write the failing tests**

Append these test cases to the end of `tests/test-live-protocol.cpp`:

```cpp
TEST_CASE("parse flags turnComplete and generationComplete on serverContent")
{
    std::string s =
        R"({"serverContent": {"generationComplete": true, "turnComplete": true}})";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.turn_complete == true);
    REQUIRE(m.generation_complete == true);
    REQUIRE(m.interrupted == false);
}

TEST_CASE("parse flags interrupted on serverContent")
{
    ServerMessage m = parse_server_message(R"({"serverContent": {"interrupted": true}})");
    REQUIRE(m.interrupted == true);
    REQUIRE(m.kind == ServerMessage::Kind::Other);
}

TEST_CASE("parse keeps audio and turnComplete in one message")
{
    std::string s = R"({
      "serverContent": { "turnComplete": true, "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.audio == std::vector<uint8_t>{1, 2, 3, 4});
    REQUIRE(m.turn_complete == true);
}

TEST_CASE("parse leaves turn flags false when absent")
{
    std::string s = R"({
      "serverContent": { "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(s);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.turn_complete == false);
    REQUIRE(m.generation_complete == false);
    REQUIRE(m.interrupted == false);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows-x64 --target unit-tests`
Expected: FAIL — compile error, `ServerMessage` has no member `turn_complete`.

- [ ] **Step 3: Add the flag fields**

In `src/live-protocol.hpp`, extend the `ServerMessage` struct (keep the existing `Kind`, `audio`, and `error_message` members):

```cpp
struct ServerMessage {
    enum class Kind { Audio, SetupComplete, Error, Other };
    Kind kind = Kind::Other;
    std::vector<uint8_t> audio;
    std::string error_message;
    bool turn_complete = false;
    bool generation_complete = false;
    bool interrupted = false;
};
```

- [ ] **Step 4: Parse the flags**

In `src/live-protocol.cpp`, replace the entire `if (j.contains("serverContent"))` block in `parse_server_message` with:

```cpp
    if (j.contains("serverContent")) {
        const auto &sc = j["serverContent"];
        m.turn_complete = sc.value("turnComplete", false);
        m.generation_complete = sc.value("generationComplete", false);
        m.interrupted = sc.value("interrupted", false);
        if (sc.contains("modelTurn") && sc["modelTurn"].contains("parts")) {
            for (const auto &part : sc["modelTurn"]["parts"]) {
                if (part.contains("inlineData") &&
                    part["inlineData"].contains("data")) {
                    auto mime = part["inlineData"].value("mimeType", "");
                    if (mime.rfind("audio/pcm", 0) == 0) {
                        m.kind = ServerMessage::Kind::Audio;
                        auto audio = base64_decode(
                            part["inlineData"]["data"].get<std::string>());
                        m.audio.insert(m.audio.end(), audio.begin(), audio.end());
                    }
                }
            }
        }
        if (m.kind == ServerMessage::Kind::Audio) return m;
        m.kind = ServerMessage::Kind::Other;
        return m;
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset windows-x64 --target unit-tests && ctest --test-dir build_x64 --output-on-failure`
Expected: PASS — all live-protocol tests pass, including the four new cases and the existing ones.

- [ ] **Step 6: Commit**

```bash
git add src/live-protocol.hpp src/live-protocol.cpp tests/test-live-protocol.cpp
git commit -m "Parse Gemini turn-boundary signals"
```

---

## Task 2: Session output notification and interrupt API

**Files:**
- Modify: `src/translation-session.hpp`
- Modify: `src/translation-session.cpp`

This task requires a full plugin build (it includes `obs.h`). There is no libobs-free unit test for it; it is verified by compiling the plugin.

- [ ] **Step 1: Declare the new members and methods**

In `src/translation-session.hpp`, add `#include <condition_variable>` near the other includes. In the `TranslationSession` public section, add these declarations (next to the existing `push_input_pcm` / `pull_output_pcm`):

```cpp
    void append_output(const uint8_t *data, size_t len);
    void signal_interrupt();
    size_t wait_and_read_output(uint8_t *out, size_t max_len, uint32_t timeout_ms);
    bool take_interrupted();
```

In the private section, add these members (next to the existing `output_` buffer):

```cpp
    std::condition_variable output_cv_;
    std::mutex output_wait_mtx_;
    std::atomic<bool> interrupted_{false};
```

- [ ] **Step 2: Implement the methods**

In `src/translation-session.cpp`, add these definitions immediately after the existing `pull_output_pcm` definition:

```cpp
void TranslationSession::append_output(const uint8_t *data, size_t len)
{
    output_.write(data, len);
    // Serialize with a consumer that is between checking the predicate and
    // entering wait_for, so the notify cannot be lost.
    { std::lock_guard<std::mutex> lk(output_wait_mtx_); }
    output_cv_.notify_one();
}

void TranslationSession::signal_interrupt()
{
    output_.clear();
    interrupted_.store(true, std::memory_order_relaxed);
    { std::lock_guard<std::mutex> lk(output_wait_mtx_); }
    output_cv_.notify_one();
}

size_t TranslationSession::wait_and_read_output(uint8_t *out, size_t max_len,
                                                uint32_t timeout_ms)
{
    {
        std::unique_lock<std::mutex> lk(output_wait_mtx_);
        output_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                            [this] { return output_.size() > 0; });
    }
    return output_.read(out, max_len);
}

bool TranslationSession::take_interrupted()
{
    return interrupted_.exchange(false, std::memory_order_relaxed);
}
```

- [ ] **Step 3: Wire the WebSocket callback to the new API**

In `src/translation-session.cpp`, inside `run()`'s `setOnMessageCallback`, replace the `ix::WebSocketMessageType::Message` branch body (the block that parses `m` and currently calls `output_.write`) with:

```cpp
            } else if (msg->type == ix::WebSocketMessageType::Message) {
                ServerMessage m = parse_server_message(msg->str);
                if (m.interrupted) {
                    blog(LOG_INFO,
                         "[live-translate] turn interrupted; dropping pending audio");
                    signal_interrupt();
                }
                if (m.kind == ServerMessage::Kind::Audio && !m.audio.empty()) {
                    blog(LOG_DEBUG, "[live-translate] received audio bytes: %zu",
                         m.audio.size());
                    append_output(m.audio.data(), m.audio.size());
                } else if (m.kind == ServerMessage::Kind::Error) {
                    blog(LOG_ERROR, "[live-translate] server error: %s",
                         m.error_message.c_str());
                    set_status(ConnStatus::AuthError, m.error_message);
                    auth_error = true;
                    ws.stop();
                }
            }
```

- [ ] **Step 4: Build the full plugin to verify it compiles**

Run: `cmake --build --preset windows-x64`
Expected: PASS — the plugin DLL builds with no errors. (`wait_and_read_output` and `take_interrupted` are defined but not yet called; that is expected — they are consumed in Task 4.)

- [ ] **Step 5: Commit**

```bash
git add src/translation-session.hpp src/translation-session.cpp
git commit -m "Add session output notification and interrupt API"
```

---

## Task 3: Pure OutputTimestamper

**Files:**
- Modify: `src/audio-pacing.hpp`
- Modify: `src/audio-pacing.cpp`
- Test: `tests/test-audio-pacing.cpp`

- [ ] **Step 1: Write the failing tests**

Append these test cases to the end of `tests/test-audio-pacing.cpp`:

```cpp
TEST_CASE("timestamper stamps the first buffer at the current clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    REQUIRE(ts.next_timestamp(1000000000ULL, 480) == 1000000000ULL);
}

TEST_CASE("timestamper keeps timestamps contiguous within a burst")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480); // advances next to 1.02e9
    // Next chunk arrives almost immediately (burst): stays contiguous, not "now".
    REQUIRE(ts.next_timestamp(1005000000ULL, 480) == 1020000000ULL);
}

TEST_CASE("timestamper clamps forward to the clock after a gap")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480);
    REQUIRE(ts.next_timestamp(5000000000ULL, 480) == 5000000000ULL);
}

TEST_CASE("timestamper reset restarts at the clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480);
    ts.reset();
    REQUIRE(ts.next_timestamp(3000000000ULL, 480) == 3000000000ULL);
}

TEST_CASE("timestamper flags excessive lead over the clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(0, 72000); // 3 s of audio at clock 0 -> lead 3 s > 2 s guard
    REQUIRE(ts.over_lead());
}

TEST_CASE("timestamper does not flag lead within the guard")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(0, 480); // 20 ms lead
    REQUIRE_FALSE(ts.over_lead());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset windows-x64 --target unit-tests`
Expected: FAIL — compile error, `OutputTimestamper` is not declared.

- [ ] **Step 3: Declare OutputTimestamper**

In `src/audio-pacing.hpp`, add this class inside `namespace lt` (leave the existing `AudioPacketShape`, `audio_packet_shape`, `audio_packet_duration_ns`, and the current `OutputJitterBuffer` / `PcmS16MonoSmoother` declarations untouched for now):

```cpp
// Produces contiguous, duration-spaced timestamps for pushed PCM so the OBS
// mixer schedules playback. Clamps forward to the wall clock after a gap and
// resets on interrupt.
class OutputTimestamper {
public:
    OutputTimestamper(uint32_t sample_rate, uint64_t max_lead_ns);
    uint64_t next_timestamp(uint64_t now_ns, size_t frames);
    void reset();
    bool over_lead() const { return over_lead_; }

private:
    uint32_t sample_rate_;
    uint64_t max_lead_ns_;
    uint64_t next_ts_ = 0;
    bool over_lead_ = false;
};
```

- [ ] **Step 4: Implement OutputTimestamper**

In `src/audio-pacing.cpp`, add these definitions inside `namespace lt` (e.g. after `audio_packet_duration_ns`):

```cpp
OutputTimestamper::OutputTimestamper(uint32_t sample_rate, uint64_t max_lead_ns)
    : sample_rate_(sample_rate), max_lead_ns_(max_lead_ns)
{}

uint64_t OutputTimestamper::next_timestamp(uint64_t now_ns, size_t frames)
{
    uint64_t ts = next_ts_ > now_ns ? next_ts_ : now_ns;
    next_ts_ = ts + static_cast<uint64_t>(frames) * 1000000000ULL / sample_rate_;
    over_lead_ = next_ts_ > now_ns + max_lead_ns_;
    return ts;
}

void OutputTimestamper::reset()
{
    next_ts_ = 0;
    over_lead_ = false;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset windows-x64 --target unit-tests && ctest --test-dir build_x64 --output-on-failure`
Expected: PASS — the six new timestamper tests pass, and all existing audio-pacing tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/audio-pacing.hpp src/audio-pacing.cpp tests/test-audio-pacing.cpp
git commit -m "Add pure output timestamper"
```

---

## Task 4: Rewrite the source as an event-driven push loop

**Files:**
- Modify: `src/source.cpp` (full rewrite of the source body)
- Modify: `src/translation-session.hpp` (remove now-unused pull declarations)
- Modify: `src/translation-session.cpp` (remove now-unused pull definitions)

This task requires a full plugin build (it includes `obs-module.h`). It is verified by compiling the plugin; runtime behaviour is verified in Task 6.

- [ ] **Step 1: Replace src/source.cpp**

Replace the **entire** contents of `src/source.cpp` with:

```cpp
#include "audio-pacing.hpp"
#include "translation-session.hpp"
#include <atomic>
#include <obs-module.h>
#include <thread>
#include <util/platform.h>
#include <vector>

namespace {

constexpr uint32_t kOutputSampleRate = 24000;
constexpr size_t kDrainCapBytes = 9600;        // 200 ms of 24 kHz mono S16
constexpr uint32_t kWaitTimeoutMs = 100;
constexpr uint64_t kMaxLeadNs = 2000000000ULL; // 2 s

struct SourceData {
    obs_source_t *context = nullptr;
    std::thread thread;
    std::atomic<bool> active{false};
    lt::OutputTimestamper timestamper{kOutputSampleRate, kMaxLeadNs};
};

const char *source_get_name(void *)
{
    return obs_module_text("Gemini Translated Audio");
}

void push_loop(SourceData *d)
{
    lt::TranslationSession &session = lt::TranslationSession::instance();
    std::vector<uint8_t> buf(kDrainCapBytes);
    while (d->active) {
        if (session.take_interrupted())
            d->timestamper.reset();

        size_t n = session.wait_and_read_output(buf.data(), buf.size(),
                                                kWaitTimeoutMs);
        if (n < sizeof(int16_t))
            continue;
        size_t frames = n / sizeof(int16_t);

        uint64_t ts = d->timestamper.next_timestamp(os_gettime_ns(), frames);
        if (d->timestamper.over_lead())
            blog(LOG_WARNING,
                 "[live-translate] translated audio running ahead of clock; "
                 "OBS may drop samples");

        struct obs_source_audio out = {};
        out.data[0] = buf.data();
        out.frames = static_cast<uint32_t>(frames);
        out.speakers = SPEAKERS_MONO;
        out.format = AUDIO_FORMAT_16BIT;
        out.samples_per_sec = kOutputSampleRate;
        out.timestamp = ts;
        obs_source_output_audio(d->context, &out);
    }
}

void *source_create(obs_data_t *, obs_source_t *source)
{
    auto *d = new SourceData();
    d->context = source;
    d->active = true;
    d->thread = std::thread(push_loop, d);
    return d;
}

void source_destroy(void *data)
{
    auto *d = static_cast<SourceData *>(data);
    d->active = false;
    if (d->thread.joinable()) d->thread.join();
    delete d;
}

} // namespace

struct obs_source_info live_translate_source_info = [] {
    struct obs_source_info info = {};
    info.id = "gemini_translated_audio_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = source_get_name;
    info.create = source_create;
    info.destroy = source_destroy;
    return info;
}();
```

- [ ] **Step 2: Remove the now-unused session pull API**

The push loop no longer polls; `pull_output_pcm` and `output_buffered_bytes` are now dead. In `src/translation-session.hpp`, delete these two declarations:

```cpp
    size_t pull_output_pcm(uint8_t *out, size_t len);
    size_t output_buffered_bytes();
```

In `src/translation-session.cpp`, delete these two definitions:

```cpp
size_t TranslationSession::pull_output_pcm(uint8_t *out, size_t len)
{
    return output_.read(out, len);
}

size_t TranslationSession::output_buffered_bytes()
{
    return output_.size();
}
```

(Keep `append_output`, which the WebSocket callback now uses, and `wait_and_read_output`, which the push loop now uses.)

- [ ] **Step 3: Build the full plugin to verify it compiles**

Run: `cmake --build --preset windows-x64`
Expected: PASS — the plugin DLL builds with no errors. (`OutputJitterBuffer` and `PcmS16MonoSmoother` are now unused but still present; they are deleted in Task 5.)

- [ ] **Step 4: Commit**

```bash
git add src/source.cpp src/translation-session.hpp src/translation-session.cpp
git commit -m "Push translated audio to OBS as it arrives"
```

---

## Task 5: Delete the dead jitter buffer and smoother

**Files:**
- Modify: `src/audio-pacing.hpp`
- Modify: `src/audio-pacing.cpp`
- Test: `tests/test-audio-pacing.cpp`

Nothing references `OutputJitterBuffer`, `PcmS16MonoSmoother`, `OutputPlaybackAction`, `output_jitter_start_bytes`, or `output_jitter_grace_ms` after Task 4. This task removes them and their tests.

- [ ] **Step 1: Remove the declarations**

In `src/audio-pacing.hpp`, delete the `output_jitter_start_bytes()` and `output_jitter_grace_ms()` declarations, the `OutputPlaybackAction` enum, the `OutputJitterBuffer` class, and the `PcmS16MonoSmoother` class. Keep `AudioPacketShape`, `audio_packet_shape`, `audio_packet_duration_ns`, and `OutputTimestamper`. The file should reduce to:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace lt {

struct AudioPacketShape {
    size_t frames;
    size_t bytes;
};

AudioPacketShape audio_packet_shape(uint32_t sample_rate, uint16_t bits_per_sample,
                                    uint16_t channels, uint32_t interval_ms);
uint64_t audio_packet_duration_ns(size_t frames, uint32_t sample_rate);

// Produces contiguous, duration-spaced timestamps for pushed PCM so the OBS
// mixer schedules playback. Clamps forward to the wall clock after a gap and
// resets on interrupt.
class OutputTimestamper {
public:
    OutputTimestamper(uint32_t sample_rate, uint64_t max_lead_ns);
    uint64_t next_timestamp(uint64_t now_ns, size_t frames);
    void reset();
    bool over_lead() const { return over_lead_; }

private:
    uint32_t sample_rate_;
    uint64_t max_lead_ns_;
    uint64_t next_ts_ = 0;
    bool over_lead_ = false;
};

}
```

- [ ] **Step 2: Remove the definitions**

In `src/audio-pacing.cpp`, delete the definitions of `output_jitter_start_bytes`, `output_jitter_grace_ms`, every `OutputJitterBuffer::` method, and `PcmS16MonoSmoother::apply`. The file should reduce to:

```cpp
#include "audio-pacing.hpp"

namespace lt {

AudioPacketShape audio_packet_shape(uint32_t sample_rate, uint16_t bits_per_sample,
                                    uint16_t channels, uint32_t interval_ms)
{
    AudioPacketShape shape{};
    shape.frames = static_cast<size_t>(sample_rate) * interval_ms / 1000;
    shape.bytes = shape.frames * channels * bits_per_sample / 8;
    return shape;
}

uint64_t audio_packet_duration_ns(size_t frames, uint32_t sample_rate)
{
    return static_cast<uint64_t>(frames) * 1000000000ULL / sample_rate;
}

OutputTimestamper::OutputTimestamper(uint32_t sample_rate, uint64_t max_lead_ns)
    : sample_rate_(sample_rate), max_lead_ns_(max_lead_ns)
{}

uint64_t OutputTimestamper::next_timestamp(uint64_t now_ns, size_t frames)
{
    uint64_t ts = next_ts_ > now_ns ? next_ts_ : now_ns;
    next_ts_ = ts + static_cast<uint64_t>(frames) * 1000000000ULL / sample_rate_;
    over_lead_ = next_ts_ > now_ns + max_lead_ns_;
    return ts;
}

void OutputTimestamper::reset()
{
    next_ts_ = 0;
    over_lead_ = false;
}

}
```

- [ ] **Step 3: Remove the dead tests**

In `tests/test-audio-pacing.cpp`, delete every test case whose name begins with `"output jitter ..."`, `"default output jitter thresholds ..."`, or `"pcm smoother ..."`. Keep the two `audio pacing ...` cases and the six `timestamper ...` cases. The file becomes:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio-pacing.hpp"

using namespace lt;

TEST_CASE("audio pacing calculates bytes and frames for a fixed interval")
{
    AudioPacketShape shape = audio_packet_shape(24000, 16, 1, 20);
    REQUIRE(shape.frames == 480);
    REQUIRE(shape.bytes == 960);
}

TEST_CASE("audio pacing calculates packet duration in nanoseconds")
{
    REQUIRE(audio_packet_duration_ns(480, 24000) == 20000000ULL);
}

TEST_CASE("timestamper stamps the first buffer at the current clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    REQUIRE(ts.next_timestamp(1000000000ULL, 480) == 1000000000ULL);
}

TEST_CASE("timestamper keeps timestamps contiguous within a burst")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480);
    REQUIRE(ts.next_timestamp(1005000000ULL, 480) == 1020000000ULL);
}

TEST_CASE("timestamper clamps forward to the clock after a gap")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480);
    REQUIRE(ts.next_timestamp(5000000000ULL, 480) == 5000000000ULL);
}

TEST_CASE("timestamper reset restarts at the clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(1000000000ULL, 480);
    ts.reset();
    REQUIRE(ts.next_timestamp(3000000000ULL, 480) == 3000000000ULL);
}

TEST_CASE("timestamper flags excessive lead over the clock")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(0, 72000);
    REQUIRE(ts.over_lead());
}

TEST_CASE("timestamper does not flag lead within the guard")
{
    OutputTimestamper ts(24000, 2000000000ULL);
    ts.next_timestamp(0, 480);
    REQUIRE_FALSE(ts.over_lead());
}
```

- [ ] **Step 4: Run the unit tests and build the full plugin**

Run: `cmake --build --preset windows-x64 --target unit-tests && ctest --test-dir build_x64 --output-on-failure`
Expected: PASS — eight audio-pacing tests pass; no reference to removed symbols.

Run: `cmake --build --preset windows-x64`
Expected: PASS — the plugin DLL builds with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/audio-pacing.hpp src/audio-pacing.cpp tests/test-audio-pacing.cpp
git commit -m "Remove dead output jitter buffer and smoother"
```

---

## Task 6: Full build and manual verification

**Files:** none (verification only)

- [ ] **Step 1: Clean full build**

Run: `cmake --build --preset windows-x64`
Expected: PASS — `build_x64\RelWithDebInfo\obs-live-translate.dll` is produced with no errors.

- [ ] **Step 2: Run the whole unit-test suite**

Run: `ctest --test-dir build_x64 --output-on-failure`
Expected: PASS — all unit tests pass (base64, ring-buffer, backoff, audio-convert, audio-pacing, live-protocol, smoke).

- [ ] **Step 3: Install and verify in OBS**

1. Close OBS. Copy `build_x64\RelWithDebInfo\obs-live-translate.dll` to `…\obs-studio\obs-plugins\64bit\obs-live-translate.dll`.
2. Launch OBS. Add the **Gemini Live Translate** filter to a mic source; enter the API key and target language. Add the **Gemini Translated Audio** source to the scene on its own track.
3. Verify by **recording** (not monitoring — monitoring can feed back and create misleading repeats):
   - [ ] Speak full sentences. Confirm the **final syllable / sentence tail is not cut off**.
   - [ ] Confirm startup latency is **materially lower** than the previous ~1.2 s (translated speech begins shortly after the source speech, bounded by network + model time).
   - [ ] Listen for **pops or dropouts**. If present, check the OBS log for the `running ahead of clock` warning (lead guard) — that indicates OBS is dropping buffered samples and the drain cap / guard may need tuning.
   - [ ] Change the API key while running, then change it back — confirm the session recovers and audio resumes.

- [ ] **Step 4: Report**

If all manual checks pass, the feature is complete. If pops appear or tails are still cut, report the OBS log lines (especially any `[live-translate]` warnings) before adjusting `kDrainCapBytes`, `kWaitTimeoutMs`, or `kMaxLeadNs`.
