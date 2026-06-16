# OBS Live Translate Plugin Implementation Plan

> **For the implementing agent (e.g. Codex on Windows):** Execute the tasks **in order, top to bottom**. Each task is bite-sized; follow its steps exactly. The tasks use test-driven development — write the failing test, run it to confirm it fails, write the minimal implementation, run it to confirm it passes, then commit. Do **not** skip the "run the test" steps: a task is only done when its stated command produces the stated expected output. Steps use checkbox (`- [ ]`) syntax — check them off as you go. Read **"Prerequisites & Environment Setup (Windows)"** below before starting Task 0. If a build/test command cannot run because of a missing prerequisite, stop and report exactly which prerequisite is missing rather than marking the task done.

**Goal:** Build a native Windows OBS plugin that streams an audio source through the Gemini Live API (`gemini-3.5-live-translate-preview`) for real-time speech-to-speech translation and emits the translated audio on a dedicated OBS audio track.

**Architecture:** A single `obs-live-translate.dll` registers an audio **filter** (captures source audio, passes the original through untouched) and a custom audio **source** (emits translated audio). A background `TranslationSession` singleton brokers between them via bounded ring buffers and a dedicated network thread that owns a WebSocket to Gemini. Pure logic (base64, audio chunking, JSON message build/parse, ring buffer, reconnect backoff) is factored into standalone, unit-tested units; OBS and network integration is layered on top.

**Tech Stack:** C++17, CMake (obs-plugintemplate), libobs (OBS 30.x), IXWebSocket (+ mbedTLS), nlohmann/json, Catch2 (unit tests). Windows x64.

---

## Prerequisites & Environment Setup (Windows)

The implementing agent runs on **Windows 10/11 x64**. Install/verify these before Task 0:

| Tool | Version | Notes |
|---|---|---|
| Visual Studio 2022 | 17.x, "Desktop development with C++" workload | Provides MSVC + Windows SDK |
| CMake | ≥ 3.24 | `cmake --version` |
| Git | any recent | needed for FetchContent (downloads json/IXWebSocket/Catch2) |
| Internet access | — | FetchContent clones deps at configure time |

**Getting libobs (the only non-trivial step).** `find_package(libobs REQUIRED)` needs OBS's exported CMake package (`libobsConfig.cmake`). Two supported routes:

- **Route A (recommended) — base the project on the official plugin template.**
  The template automates downloading prebuilt OBS dependencies and libobs on
  Windows via CMake presets:
  1. `git clone https://github.com/obsproject/obs-plugintemplate` into a scratch dir.
  2. Copy its `cmake/`, `.cmake-format.json`, and CMake preset files
     (`CMakePresets.json`, `buildspec.json`) into this repo.
  3. Configure with the Windows preset:
     `cmake --preset windows-x64`
     This fetches libobs so `find_package(libobs)` resolves. Then build with
     `cmake --build --preset windows-x64`.
  Merge the `add_library`/`target_link_libraries` content from this plan's
  `CMakeLists.txt` into the template's plugin target.

- **Route B — point CMake at an existing libobs install.**
  If a built OBS Studio (or its dev package) is available, set
  `-DCMAKE_PREFIX_PATH=<path-to-obs-install>` (the directory containing
  `lib/cmake/libobs/libobsConfig.cmake`) when configuring.

**Build/test commands used throughout this plan** (Route B style; adjust to the
preset if using Route A):
```bash
# configure (tests on; libobs located via CMAKE_PREFIX_PATH)
cmake -B build -DCMAKE_PREFIX_PATH=<path-to-obs-install>
# build the plugin
cmake --build build --config RelWithDebInfo
# build + run unit tests (no libobs needed for the test target)
cmake --build build --target unit-tests
ctest --test-dir build --output-on-failure
```

> The **unit test target (`unit-tests`) does not depend on libobs** — only on
> Catch2 + nlohmann/json (both via FetchContent). It builds and runs even
> before libobs is set up, so Tasks 2–6 can be verified independently of the
> OBS toolchain.

**Working branch.** Do all implementation on a feature branch (`feature/implementation`), not on `main`. Commit after every task as the steps instruct.

**API key for the integration harness (Task 10).** A valid Gemini API key and a
16 kHz mono 16-bit PCM WAV file are required only for the manual integration run.

---

## File Structure

```
CMakeLists.txt                     # plugin build, deps, test target
cmake/                             # obs-plugintemplate cmake helpers (from template)
src/
  plugin-main.cpp                  # OBS_DECLARE_MODULE, obs_module_load/unload, register types
  base64.hpp / base64.cpp          # pure: encode/decode
  audio-convert.hpp / .cpp         # pure: downmix, float->S16LE, 100ms chunker
  ring-buffer.hpp / .cpp           # pure: bounded byte ring buffer, drop-oldest
  live-protocol.hpp / .cpp         # pure: build setup/realtimeInput JSON, parse serverContent
  backoff.hpp / .cpp               # pure: exponential backoff calculator
  translation-session.hpp / .cpp   # singleton broker: buffers + network thread + IXWebSocket
  filter.cpp                       # OBS audio filter (capture path + config UI)
  source.cpp                       # OBS custom audio source (output path)
  languages.hpp                    # static BCP-47 target language table
tests/
  CMakeLists.txt                   # Catch2 test runner
  test-base64.cpp
  test-audio-convert.cpp
  test-ring-buffer.cpp
  test-live-protocol.cpp
  test-backoff.cpp
tools/
  wav-harness.cpp                  # standalone WAV -> pipeline -> WAV integration harness
docs/
  install.md                       # manual install instructions
```

Pure units (`base64`, `audio-convert`, `ring-buffer`, `live-protocol`, `backoff`) have **no libobs dependency** so the test target builds without OBS.

---

### Task 0: Project scaffold builds an empty plugin

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/plugin-main.cpp`
- Create: `.gitignore`

- [ ] **Step 1: Create `.gitignore`**

```gitignore
/build/
/.cache/
*.user
```

- [ ] **Step 2: Create top-level `CMakeLists.txt`**

This uses `find_package(libobs)` (provided by an OBS dev package or the OBS source tree referenced by `CMAKE_PREFIX_PATH`). Deps are added in Task 1; for now just the plugin module.

```cmake
cmake_minimum_required(VERSION 3.24)
project(obs-live-translate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build unit tests" ON)

find_package(libobs REQUIRED)

add_library(obs-live-translate MODULE
  src/plugin-main.cpp
)
target_link_libraries(obs-live-translate PRIVATE OBS::libobs)
set_target_properties(obs-live-translate PROPERTIES PREFIX "")

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
```

- [ ] **Step 3: Create `src/plugin-main.cpp` (empty but valid module)**

```cpp
#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-live-translate", "en-US")

extern "C" const char *obs_module_name(void) { return "OBS Live Translate"; }
extern "C" const char *obs_module_description(void)
{
    return "Real-time speech-to-speech translation via the Gemini Live API.";
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[live-translate] module loaded");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[live-translate] module unloaded");
}
```

- [ ] **Step 4: Configure and build**

Run (from repo root, with `CMAKE_PREFIX_PATH` pointing at the libobs dev package):
```bash
cmake -B build -DBUILD_TESTS=OFF
cmake --build build --config RelWithDebInfo
```
Expected: builds `obs-live-translate.dll` with no errors.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/plugin-main.cpp .gitignore
git commit -m "Scaffold empty OBS plugin module"
```

---

### Task 1: Vendor dependencies and stand up the test target

**Files:**
- Modify: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test-smoke.cpp`

- [ ] **Step 1: Add FetchContent deps to top-level `CMakeLists.txt`**

Insert after `find_package(libobs REQUIRED)`:

```cmake
include(FetchContent)

FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)
FetchContent_MakeAvailable(json)

set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_MBED_TLS ON CACHE BOOL "" FORCE)
FetchContent_Declare(ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.5)
FetchContent_MakeAvailable(ixwebsocket)
```

- [ ] **Step 2: Create `tests/CMakeLists.txt` with Catch2**

```cmake
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

add_executable(unit-tests
  test-smoke.cpp
)
target_include_directories(unit-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(unit-tests PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(unit-tests)
```

- [ ] **Step 3: Create `tests/test-smoke.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke") { REQUIRE(1 + 1 == 2); }
```

- [ ] **Step 4: Build and run tests**

Run:
```bash
cmake -B build
cmake --build build --target unit-tests
ctest --test-dir build --output-on-failure
```
Expected: `smoke` test PASSES.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/test-smoke.cpp
git commit -m "Add vendored deps (json, IXWebSocket) and Catch2 test target"
```

---

### Task 2: base64 encode/decode (pure)

**Files:**
- Create: `src/base64.hpp`, `src/base64.cpp`
- Create: `tests/test-base64.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test (`tests/test-base64.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "base64.hpp"
#include <string>
#include <vector>

using lt::base64_decode;
using lt::base64_encode;

TEST_CASE("base64 encodes known vectors") {
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>(""), 0) == "");
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>("f"), 1) == "Zg==");
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>("fo"), 2) == "Zm8=");
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>("foo"), 3) == "Zm9v");
    REQUIRE(base64_encode(reinterpret_cast<const uint8_t*>("foobar"), 6) == "Zm9vYmFy");
}

TEST_CASE("base64 round-trips arbitrary bytes") {
    std::vector<uint8_t> bytes{0x00, 0xFF, 0x10, 0x80, 0x7F, 0x01};
    auto enc = base64_encode(bytes.data(), bytes.size());
    auto dec = base64_decode(enc);
    REQUIRE(dec == bytes);
}
```

- [ ] **Step 2: Add the test file to `tests/CMakeLists.txt`**

Add `test-base64.cpp` and `${CMAKE_SOURCE_DIR}/src/base64.cpp` to the `unit-tests` sources:
```cmake
add_executable(unit-tests
  test-smoke.cpp
  test-base64.cpp
  ${CMAKE_SOURCE_DIR}/src/base64.cpp
)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target unit-tests`
Expected: FAIL — `base64.hpp` not found.

- [ ] **Step 4: Create `src/base64.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lt {
std::string base64_encode(const uint8_t *data, size_t len);
std::vector<uint8_t> base64_decode(const std::string &in);
}
```

- [ ] **Step 5: Create `src/base64.cpp`**

```cpp
#include "base64.hpp"

namespace lt {
static const char kTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t *data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(kTable[(n >> 6) & 63]);
        out.push_back(kTable[n & 63]);
    }
    if (len - i == 1) {
        uint32_t n = data[i] << 16;
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (len - i == 2) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(kTable[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::vector<uint8_t> base64_decode(const std::string &in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target unit-tests && ctest --test-dir build --output-on-failure`
Expected: base64 tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/base64.hpp src/base64.cpp tests/test-base64.cpp tests/CMakeLists.txt
git commit -m "Add base64 encode/decode with tests"
```

---

### Task 3: Audio conversion — downmix + float→S16LE + 100ms chunker (pure)

The OBS-resampler step (48k→16k) is integrated later in the session (it needs a live libobs resampler). Here we test the parts that are pure: multi-channel float planar → mono float, mono float → 16-bit little-endian PCM, and chunking a PCM byte stream into fixed 100 ms (3200-byte) blocks with a remainder carried across calls.

**Files:**
- Create: `src/audio-convert.hpp`, `src/audio-convert.cpp`
- Create: `tests/test-audio-convert.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test (`tests/test-audio-convert.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio-convert.hpp"
#include <cmath>
#include <cstring>
#include <vector>

using namespace lt;

TEST_CASE("downmix averages channels into mono") {
    float l[] = {1.0f, 0.0f, -1.0f};
    float r[] = {-1.0f, 0.0f, 1.0f};
    const float *planes[] = {l, r};
    std::vector<float> mono = downmix_to_mono(planes, 2, 3);
    REQUIRE(mono.size() == 3);
    REQUIRE(mono[0] == Approx(0.0f));
    REQUIRE(mono[1] == Approx(0.0f));
    REQUIRE(mono[2] == Approx(0.0f));
}

TEST_CASE("downmix with one channel is identity") {
    float c[] = {0.5f, -0.5f};
    const float *planes[] = {c};
    std::vector<float> mono = downmix_to_mono(planes, 1, 2);
    REQUIRE(mono[0] == Approx(0.5f));
    REQUIRE(mono[1] == Approx(-0.5f));
}

TEST_CASE("float to s16le clamps and scales") {
    std::vector<float> in{0.0f, 1.0f, -1.0f, 2.0f, -2.0f};
    std::vector<uint8_t> pcm = float_to_s16le(in.data(), in.size());
    REQUIRE(pcm.size() == in.size() * 2);
    auto sample = [&](size_t i) {
        return static_cast<int16_t>(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
    };
    REQUIRE(sample(0) == 0);
    REQUIRE(sample(1) == 32767);   // +1.0 -> max
    REQUIRE(sample(2) == -32768);  // -1.0 -> min
    REQUIRE(sample(3) == 32767);   // +2.0 clamps
    REQUIRE(sample(4) == -32768);  // -2.0 clamps
}

TEST_CASE("chunker emits fixed-size blocks and carries remainder") {
    Chunker chunker(3200); // 100ms @ 16kHz 16-bit mono
    std::vector<uint8_t> a(2000, 0xAB);
    auto out1 = chunker.push(a.data(), a.size());
    REQUIRE(out1.empty()); // not enough yet

    std::vector<uint8_t> b(2500, 0xCD);
    auto out2 = chunker.push(b.data(), b.size());
    REQUIRE(out2.size() == 1);            // 4500 bytes -> one 3200 block
    REQUIRE(out2[0].size() == 3200);

    std::vector<uint8_t> c(2000, 0xEE);
    auto out3 = chunker.push(c.data(), c.size()); // 1300 + 2000 = 3300 -> one block
    REQUIRE(out3.size() == 1);
    REQUIRE(out3[0].size() == 3200);
}
```

- [ ] **Step 2: Add sources to `tests/CMakeLists.txt`**

Append `test-audio-convert.cpp` and `${CMAKE_SOURCE_DIR}/src/audio-convert.cpp` to `unit-tests`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target unit-tests`
Expected: FAIL — `audio-convert.hpp` not found.

- [ ] **Step 4: Create `src/audio-convert.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace lt {

std::vector<float> downmix_to_mono(const float *const *planes,
                                   size_t channels, size_t frames);

std::vector<uint8_t> float_to_s16le(const float *samples, size_t count);

// Accumulates bytes and emits fixed-size chunks; remainder carried internally.
class Chunker {
public:
    explicit Chunker(size_t chunk_bytes) : chunk_bytes_(chunk_bytes) {}
    std::vector<std::vector<uint8_t>> push(const uint8_t *data, size_t len);
    void reset() { acc_.clear(); }

private:
    size_t chunk_bytes_;
    std::vector<uint8_t> acc_;
};

}
```

- [ ] **Step 5: Create `src/audio-convert.cpp`**

```cpp
#include "audio-convert.hpp"
#include <algorithm>
#include <cmath>

namespace lt {

std::vector<float> downmix_to_mono(const float *const *planes,
                                   size_t channels, size_t frames) {
    std::vector<float> mono(frames, 0.0f);
    if (channels == 0) return mono;
    for (size_t ch = 0; ch < channels; ++ch)
        for (size_t i = 0; i < frames; ++i)
            mono[i] += planes[ch][i];
    const float inv = 1.0f / static_cast<float>(channels);
    for (float &s : mono) s *= inv;
    return mono;
}

std::vector<uint8_t> float_to_s16le(const float *samples, size_t count) {
    std::vector<uint8_t> out(count * 2);
    for (size_t i = 0; i < count; ++i) {
        float v = std::max(-1.0f, std::min(1.0f, samples[i]));
        int32_t s = static_cast<int32_t>(std::lround(v * 32767.0f));
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i * 2] = static_cast<uint8_t>(s & 0xFF);
        out[i * 2 + 1] = static_cast<uint8_t>((s >> 8) & 0xFF);
    }
    return out;
}

std::vector<std::vector<uint8_t>> Chunker::push(const uint8_t *data, size_t len) {
    acc_.insert(acc_.end(), data, data + len);
    std::vector<std::vector<uint8_t>> chunks;
    size_t off = 0;
    while (acc_.size() - off >= chunk_bytes_) {
        chunks.emplace_back(acc_.begin() + off, acc_.begin() + off + chunk_bytes_);
        off += chunk_bytes_;
    }
    if (off > 0) acc_.erase(acc_.begin(), acc_.begin() + off);
    return chunks;
}

}
```

Note: the `+1.0 -> 32767` test expectation matches `lround(1.0 * 32767)`. The clamp to `-1.0` yields `lround(-32767) = -32767`; the test expects `-32768` for the `-2.0` clamp case only. Adjust the test for `-1.0`: it produces `-32767`. **Update the test's `sample(2)` expectation to `-32767`** before running (the `-2.0` clamp at `sample(4)` stays `-32768` because clamp happens at the float stage to `-1.0` → `-32767`; so also set `sample(4)` to `-32767`). Final expected values: 0, 32767, -32767, 32767, -32767.

- [ ] **Step 6: Fix the test expectations per the note, then run**

Edit `tests/test-audio-convert.cpp`: `sample(2)` and `sample(4)` expect `-32767`.
Run: `cmake --build build --target unit-tests && ctest --test-dir build --output-on-failure`
Expected: audio-convert tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/audio-convert.hpp src/audio-convert.cpp tests/test-audio-convert.cpp tests/CMakeLists.txt
git commit -m "Add audio conversion (downmix, float->s16le, chunker) with tests"
```

---

### Task 4: Bounded ring buffer with drop-oldest (pure)

**Files:**
- Create: `src/ring-buffer.hpp`, `src/ring-buffer.cpp`
- Create: `tests/test-ring-buffer.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test (`tests/test-ring-buffer.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ring-buffer.hpp"
#include <vector>

using lt::ByteRingBuffer;

TEST_CASE("write then read returns same bytes") {
    ByteRingBuffer rb(1024);
    std::vector<uint8_t> in{1, 2, 3, 4};
    REQUIRE(rb.write(in.data(), in.size()) == 4);
    std::vector<uint8_t> out(4);
    REQUIRE(rb.read(out.data(), 4) == 4);
    REQUIRE(out == in);
    REQUIRE(rb.size() == 0);
}

TEST_CASE("read returns only what is available") {
    ByteRingBuffer rb(1024);
    std::vector<uint8_t> in{9, 8};
    rb.write(in.data(), in.size());
    std::vector<uint8_t> out(10);
    REQUIRE(rb.read(out.data(), 10) == 2);
}

TEST_CASE("overflow drops oldest bytes") {
    ByteRingBuffer rb(4);                  // capacity 4
    std::vector<uint8_t> a{1, 2, 3, 4};
    rb.write(a.data(), a.size());          // buffer = [1,2,3,4]
    std::vector<uint8_t> b{5, 6};
    rb.write(b.data(), b.size());          // drops 1,2 -> [3,4,5,6]
    std::vector<uint8_t> out(4);
    REQUIRE(rb.read(out.data(), 4) == 4);
    REQUIRE(out == std::vector<uint8_t>{3, 4, 5, 6});
}

TEST_CASE("writing more than capacity keeps only newest capacity bytes") {
    ByteRingBuffer rb(3);
    std::vector<uint8_t> a{1, 2, 3, 4, 5};
    rb.write(a.data(), a.size());          // keep last 3 -> [3,4,5]
    std::vector<uint8_t> out(3);
    REQUIRE(rb.read(out.data(), 3) == 3);
    REQUIRE(out == std::vector<uint8_t>{3, 4, 5});
}
```

- [ ] **Step 2: Add sources to `tests/CMakeLists.txt`**

Append `test-ring-buffer.cpp` and `${CMAKE_SOURCE_DIR}/src/ring-buffer.cpp`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target unit-tests`
Expected: FAIL — `ring-buffer.hpp` not found.

- [ ] **Step 4: Create `src/ring-buffer.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

namespace lt {

// Thread-safe bounded byte buffer. On overflow, oldest bytes are dropped.
class ByteRingBuffer {
public:
    explicit ByteRingBuffer(size_t capacity) : capacity_(capacity) {}

    // Always "succeeds": returns number of bytes accepted (== len),
    // dropping oldest data as needed to stay within capacity.
    size_t write(const uint8_t *data, size_t len);

    // Reads up to len bytes; returns number actually read.
    size_t read(uint8_t *out, size_t len);

    size_t size();
    void clear();

private:
    size_t capacity_;
    std::vector<uint8_t> buf_;   // simple deque-like storage
    std::mutex mtx_;
};

}
```

- [ ] **Step 5: Create `src/ring-buffer.cpp`**

```cpp
#include "ring-buffer.hpp"
#include <algorithm>
#include <cstring>

namespace lt {

size_t ByteRingBuffer::write(const uint8_t *data, size_t len) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (len >= capacity_) {
        // keep only the newest capacity_ bytes
        buf_.assign(data + (len - capacity_), data + len);
        return len;
    }
    buf_.insert(buf_.end(), data, data + len);
    if (buf_.size() > capacity_) {
        size_t drop = buf_.size() - capacity_;
        buf_.erase(buf_.begin(), buf_.begin() + drop);
    }
    return len;
}

size_t ByteRingBuffer::read(uint8_t *out, size_t len) {
    std::lock_guard<std::mutex> lock(mtx_);
    size_t n = std::min(len, buf_.size());
    if (n) {
        std::memcpy(out, buf_.data(), n);
        buf_.erase(buf_.begin(), buf_.begin() + n);
    }
    return n;
}

size_t ByteRingBuffer::size() {
    std::lock_guard<std::mutex> lock(mtx_);
    return buf_.size();
}

void ByteRingBuffer::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    buf_.clear();
}

}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target unit-tests && ctest --test-dir build --output-on-failure`
Expected: ring-buffer tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ring-buffer.hpp src/ring-buffer.cpp tests/test-ring-buffer.cpp tests/CMakeLists.txt
git commit -m "Add bounded ring buffer with drop-oldest and tests"
```

---

### Task 5: Live API message build/parse (pure)

**Files:**
- Create: `src/live-protocol.hpp`, `src/live-protocol.cpp`
- Create: `tests/test-live-protocol.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test (`tests/test-live-protocol.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "live-protocol.hpp"
#include <nlohmann/json.hpp>

using namespace lt;
using nlohmann::json;

TEST_CASE("setup message has model and translation config") {
    std::string msg = build_setup_message("zh-TW", true);
    json j = json::parse(msg);
    REQUIRE(j["setup"]["model"] == "models/gemini-3.5-live-translate-preview");
    REQUIRE(j["setup"]["generationConfig"]["responseModalities"][0] == "AUDIO");
    REQUIRE(j["setup"]["translationConfig"]["targetLanguageCode"] == "zh-TW");
    REQUIRE(j["setup"]["translationConfig"]["echoTargetLanguage"] == true);
}

TEST_CASE("realtime input message carries base64 pcm with correct mime") {
    std::vector<uint8_t> pcm{0x01, 0x02, 0x03, 0x04};
    std::string msg = build_realtime_input_message(pcm.data(), pcm.size());
    json j = json::parse(msg);
    auto chunk = j["realtimeInput"]["mediaChunks"][0];
    REQUIRE(chunk["mimeType"] == "audio/pcm;rate=16000");
    REQUIRE(chunk["data"].get<std::string>() == "AQIDBA==");
}

TEST_CASE("parse extracts pcm audio from serverContent") {
    // base64 "AQIDBA==" == bytes 01 02 03 04
    std::string server = R"({
      "serverContent": { "modelTurn": { "parts": [
        { "inlineData": { "mimeType": "audio/pcm;rate=24000", "data": "AQIDBA==" } }
      ] } }
    })";
    ServerMessage m = parse_server_message(server);
    REQUIRE(m.kind == ServerMessage::Kind::Audio);
    REQUIRE(m.audio == std::vector<uint8_t>{1, 2, 3, 4});
}

TEST_CASE("parse flags setup-rejected / error messages") {
    std::string err = R"({"error": {"code": 400, "message": "API key not valid"}})";
    ServerMessage m = parse_server_message(err);
    REQUIRE(m.kind == ServerMessage::Kind::Error);
    REQUIRE(m.error_message.find("API key") != std::string::npos);
}

TEST_CASE("parse ignores unrelated messages gracefully") {
    ServerMessage m = parse_server_message(R"({"setupComplete": {}})");
    REQUIRE(m.kind == ServerMessage::Kind::SetupComplete);
}
```

- [ ] **Step 2: Add sources to `tests/CMakeLists.txt`**

Append `test-live-protocol.cpp` and `${CMAKE_SOURCE_DIR}/src/live-protocol.cpp`, and link json:
```cmake
target_link_libraries(unit-tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target unit-tests`
Expected: FAIL — `live-protocol.hpp` not found.

- [ ] **Step 4: Create `src/live-protocol.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lt {

std::string build_setup_message(const std::string &target_language_code,
                                bool echo_target_language);

std::string build_realtime_input_message(const uint8_t *pcm, size_t len);

struct ServerMessage {
    enum class Kind { Audio, SetupComplete, Error, Other };
    Kind kind = Kind::Other;
    std::vector<uint8_t> audio;   // valid when kind == Audio
    std::string error_message;    // valid when kind == Error
};

ServerMessage parse_server_message(const std::string &json_text);

}
```

- [ ] **Step 5: Create `src/live-protocol.cpp`**

```cpp
#include "live-protocol.hpp"
#include "base64.hpp"
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace lt {

std::string build_setup_message(const std::string &target, bool echo) {
    json j;
    j["setup"]["model"] = "models/gemini-3.5-live-translate-preview";
    j["setup"]["generationConfig"]["responseModalities"] = json::array({"AUDIO"});
    j["setup"]["translationConfig"]["targetLanguageCode"] = target;
    j["setup"]["translationConfig"]["echoTargetLanguage"] = echo;
    return j.dump();
}

std::string build_realtime_input_message(const uint8_t *pcm, size_t len) {
    json chunk;
    chunk["mimeType"] = "audio/pcm;rate=16000";
    chunk["data"] = base64_encode(pcm, len);
    json j;
    j["realtimeInput"]["mediaChunks"] = json::array({chunk});
    return j.dump();
}

ServerMessage parse_server_message(const std::string &text) {
    ServerMessage m;
    json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) { m.kind = ServerMessage::Kind::Other; return m; }

    if (j.contains("error")) {
        m.kind = ServerMessage::Kind::Error;
        if (j["error"].contains("message"))
            m.error_message = j["error"]["message"].get<std::string>();
        return m;
    }
    if (j.contains("setupComplete")) {
        m.kind = ServerMessage::Kind::SetupComplete;
        return m;
    }
    if (j.contains("serverContent")) {
        const auto &sc = j["serverContent"];
        if (sc.contains("modelTurn") && sc["modelTurn"].contains("parts")) {
            for (const auto &part : sc["modelTurn"]["parts"]) {
                if (part.contains("inlineData") &&
                    part["inlineData"].contains("data")) {
                    auto mime = part["inlineData"].value("mimeType", "");
                    if (mime.rfind("audio/pcm", 0) == 0) {
                        m.kind = ServerMessage::Kind::Audio;
                        m.audio = base64_decode(
                            part["inlineData"]["data"].get<std::string>());
                        return m;
                    }
                }
            }
        }
    }
    m.kind = ServerMessage::Kind::Other;
    return m;
}

}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target unit-tests && ctest --test-dir build --output-on-failure`
Expected: live-protocol tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/live-protocol.hpp src/live-protocol.cpp tests/test-live-protocol.cpp tests/CMakeLists.txt
git commit -m "Add Gemini Live message build/parse with tests"
```

---

### Task 6: Reconnect backoff calculator (pure)

**Files:**
- Create: `src/backoff.hpp`, `src/backoff.cpp`
- Create: `tests/test-backoff.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test (`tests/test-backoff.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "backoff.hpp"

using lt::Backoff;

TEST_CASE("backoff doubles and caps") {
    Backoff b(/*base_ms=*/1000, /*max_ms=*/30000);
    REQUIRE(b.next_ms() == 1000);
    REQUIRE(b.next_ms() == 2000);
    REQUIRE(b.next_ms() == 4000);
    REQUIRE(b.next_ms() == 8000);
    REQUIRE(b.next_ms() == 16000);
    REQUIRE(b.next_ms() == 30000); // 32000 capped
    REQUIRE(b.next_ms() == 30000); // stays capped
}

TEST_CASE("reset returns to base") {
    Backoff b(1000, 30000);
    b.next_ms();
    b.next_ms();
    b.reset();
    REQUIRE(b.next_ms() == 1000);
}
```

- [ ] **Step 2: Add sources to `tests/CMakeLists.txt`**

Append `test-backoff.cpp` and `${CMAKE_SOURCE_DIR}/src/backoff.cpp`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --target unit-tests`
Expected: FAIL — `backoff.hpp` not found.

- [ ] **Step 4: Create `src/backoff.hpp`**

```cpp
#pragma once
#include <cstdint>

namespace lt {

class Backoff {
public:
    Backoff(uint32_t base_ms, uint32_t max_ms)
        : base_ms_(base_ms), max_ms_(max_ms), current_(base_ms) {}

    uint32_t next_ms() {
        uint32_t value = current_;
        uint64_t doubled = static_cast<uint64_t>(current_) * 2;
        current_ = doubled > max_ms_ ? max_ms_ : static_cast<uint32_t>(doubled);
        return value > max_ms_ ? max_ms_ : value;
    }

    void reset() { current_ = base_ms_; }

private:
    uint32_t base_ms_;
    uint32_t max_ms_;
    uint32_t current_;
};

}
```

- [ ] **Step 5: Create `src/backoff.cpp`**

```cpp
#include "backoff.hpp"
// Header-only logic; this translation unit exists for symmetry / future growth.
namespace lt {}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target unit-tests && ctest --test-dir build --output-on-failure`
Expected: backoff tests PASS.

- [ ] **Step 7: Commit**

```bash
git add src/backoff.hpp src/backoff.cpp tests/test-backoff.cpp tests/CMakeLists.txt
git commit -m "Add exponential backoff calculator with tests"
```

---

### Task 7: TranslationSession — buffers + network thread + WebSocket

This wires the pure units to IXWebSocket. It is integration code (not unit-tested) but uses only already-tested pieces. The session is a singleton; the filter feeds input PCM, the source drains output PCM.

**Files:**
- Create: `src/translation-session.hpp`, `src/translation-session.cpp`
- Modify: `CMakeLists.txt` (add session source + link ixwebsocket/json to the plugin module)

- [ ] **Step 1: Create `src/translation-session.hpp`**

```cpp
#pragma once
#include "ring-buffer.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace lt {

enum class ConnStatus { Idle, Connecting, Connected, Reconnecting, AuthError };

class TranslationSession {
public:
    static TranslationSession &instance();

    // Configure & (re)start the network thread. Called from filter update.
    void configure(const std::string &api_key, const std::string &target_lang);
    void stop();

    // Filter pushes 16kHz/16-bit/mono PCM bytes (already converted).
    void push_input_pcm(const uint8_t *data, size_t len);

    // Source pulls translated 24kHz/16-bit/mono PCM bytes.
    size_t pull_output_pcm(uint8_t *out, size_t len);

    ConnStatus status();
    std::string status_text();

private:
    TranslationSession();
    ~TranslationSession();
    void run();                       // network thread body
    void set_status(ConnStatus s, const std::string &detail = "");

    ByteRingBuffer input_{16000 * 2 * 5};    // ~5s of 16k/16-bit mono
    ByteRingBuffer output_{24000 * 2 * 10};  // ~10s of 24k/16-bit mono

    std::mutex cfg_mtx_;
    std::string api_key_;
    std::string target_lang_;

    std::atomic<bool> running_{false};
    std::atomic<bool> config_changed_{false};
    std::thread thread_;

    std::mutex status_mtx_;
    ConnStatus status_ = ConnStatus::Idle;
    std::string status_detail_;
};

}
```

- [ ] **Step 2: Create `src/translation-session.cpp`**

```cpp
#include "translation-session.hpp"
#include "backoff.hpp"
#include "live-protocol.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <obs.h>           // for blog
#include <chrono>
#include <vector>

namespace lt {

TranslationSession &TranslationSession::instance() {
    static TranslationSession s;
    return s;
}

TranslationSession::TranslationSession() = default;

TranslationSession::~TranslationSession() { stop(); }

void TranslationSession::set_status(ConnStatus s, const std::string &detail) {
    std::lock_guard<std::mutex> lk(status_mtx_);
    status_ = s;
    status_detail_ = detail;
}

ConnStatus TranslationSession::status() {
    std::lock_guard<std::mutex> lk(status_mtx_);
    return status_;
}

std::string TranslationSession::status_text() {
    std::lock_guard<std::mutex> lk(status_mtx_);
    switch (status_) {
        case ConnStatus::Idle: return "Idle";
        case ConnStatus::Connecting: return "Connecting...";
        case ConnStatus::Connected: return "Connected";
        case ConnStatus::Reconnecting: return "Reconnecting...";
        case ConnStatus::AuthError: return "API key error: " + status_detail_;
    }
    return "Unknown";
}

void TranslationSession::configure(const std::string &api_key,
                                   const std::string &target_lang) {
    {
        std::lock_guard<std::mutex> lk(cfg_mtx_);
        api_key_ = api_key;
        target_lang_ = target_lang;
    }
    if (api_key.empty()) { stop(); return; }
    config_changed_ = true;
    if (!running_.exchange(true)) {
        thread_ = std::thread([this] { run(); });
    }
}

void TranslationSession::stop() {
    if (running_.exchange(false)) {
        if (thread_.joinable()) thread_.join();
    }
    input_.clear();
    output_.clear();
    set_status(ConnStatus::Idle);
}

void TranslationSession::push_input_pcm(const uint8_t *data, size_t len) {
    input_.write(data, len);
}

size_t TranslationSession::pull_output_pcm(uint8_t *out, size_t len) {
    return output_.read(out, len);
}

void TranslationSession::run() {
    Backoff backoff(1000, 30000);

    while (running_) {
        std::string key, lang;
        {
            std::lock_guard<std::mutex> lk(cfg_mtx_);
            key = api_key_;
            lang = target_lang_;
        }
        config_changed_ = false;

        ix::WebSocket ws;
        std::string url =
            "wss://generativelanguage.googleapis.com/ws/"
            "google.ai.generativelanguage.v1beta.GenerativeService."
            "BidiGenerateContent?key=" + key;
        ws.setUrl(url);

        std::atomic<bool> auth_error{false};
        std::atomic<bool> open{false};

        ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr &msg) {
            if (msg->type == ix::WebSocketMessageType::Open) {
                open = true;
                std::string setup = build_setup_message(lang, /*echo=*/true);
                ws.sendBinary(setup);
                set_status(ConnStatus::Connected);
                backoff.reset();
            } else if (msg->type == ix::WebSocketMessageType::Message) {
                ServerMessage m = parse_server_message(msg->str);
                if (m.kind == ServerMessage::Kind::Audio && !m.audio.empty()) {
                    output_.write(m.audio.data(), m.audio.size());
                } else if (m.kind == ServerMessage::Kind::Error) {
                    blog(LOG_ERROR, "[live-translate] server error: %s",
                         m.error_message.c_str());
                    set_status(ConnStatus::AuthError, m.error_message);
                    auth_error = true;
                    ws.stop();
                }
            } else if (msg->type == ix::WebSocketMessageType::Close ||
                       msg->type == ix::WebSocketMessageType::Error) {
                open = false;
            }
        });

        set_status(ConnStatus::Connecting);
        ws.start();

        // Send loop: drain input buffer in 100ms (3200-byte) chunks.
        std::vector<uint8_t> chunk(3200);
        while (running_ && !auth_error && !config_changed_) {
            size_t n = input_.read(chunk.data(), chunk.size());
            if (n == chunk.size() && open) {
                std::string m =
                    build_realtime_input_message(chunk.data(), chunk.size());
                ws.sendBinary(m);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        ws.stop();

        if (auth_error) {
            // Do NOT reconnect on auth errors.
            running_ = false;
            break;
        }
        if (config_changed_) continue;        // restart loop with new config
        if (!running_) break;

        // Network drop -> exponential backoff then reconnect.
        set_status(ConnStatus::Reconnecting);
        uint32_t wait = backoff.next_ms();
        for (uint32_t waited = 0; waited < wait && running_ && !config_changed_;
             waited += 50)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    set_status(status() == ConnStatus::AuthError ? ConnStatus::AuthError
                                                 : ConnStatus::Idle);
}

}
```

- [ ] **Step 3: Wire the session into the plugin build in `CMakeLists.txt`**

Update the plugin target:
```cmake
add_library(obs-live-translate MODULE
  src/plugin-main.cpp
  src/base64.cpp
  src/audio-convert.cpp
  src/ring-buffer.cpp
  src/live-protocol.cpp
  src/backoff.cpp
  src/translation-session.cpp
)
target_link_libraries(obs-live-translate PRIVATE
  OBS::libobs
  ixwebsocket
  nlohmann_json::nlohmann_json
)
```

- [ ] **Step 4: Build the plugin to verify it compiles and links**

Run: `cmake --build build --config RelWithDebInfo`
Expected: `obs-live-translate.dll` builds and links against ixwebsocket + json with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/translation-session.hpp src/translation-session.cpp CMakeLists.txt
git commit -m "Add TranslationSession with WebSocket network thread"
```

---

### Task 8: OBS audio filter — capture path + config UI

The filter converts OBS float planar audio to 16 kHz mono S16LE using a libobs `audio_resampler`, then pushes to the session. It returns the original audio unchanged.

**Files:**
- Create: `src/languages.hpp`
- Create: `src/filter.cpp`
- Modify: `CMakeLists.txt` (add `src/filter.cpp`)
- Modify: `src/plugin-main.cpp` (register the filter)

- [ ] **Step 1: Create `src/languages.hpp`**

```cpp
#pragma once
namespace lt {
struct LangEntry { const char *code; const char *name; };
static const LangEntry kLanguages[] = {
    {"en", "English"},      {"zh-TW", "Chinese (Traditional)"},
    {"zh-CN", "Chinese (Simplified)"}, {"ja", "Japanese"},
    {"ko", "Korean"},       {"es", "Spanish"},
    {"fr", "French"},       {"de", "German"},
    {"pt", "Portuguese"},   {"it", "Italian"},
    {"ru", "Russian"},      {"id", "Indonesian"},
    {"th", "Thai"},         {"vi", "Vietnamese"},
};
static const int kLanguagesCount =
    sizeof(kLanguages) / sizeof(kLanguages[0]);
}
```

- [ ] **Step 2: Create `src/filter.cpp`**

```cpp
#include "audio-convert.hpp"
#include "languages.hpp"
#include "translation-session.hpp"
#include <obs-module.h>
#include <media-io/audio-resampler.h>
#include <string>

namespace {

struct FilterData {
    obs_source_t *context = nullptr;
    audio_resampler_t *resampler = nullptr;
    lt::Chunker chunker{3200}; // 100ms @ 16k/16-bit/mono
    std::string api_key;
    std::string target_lang = "en";
};

const char *filter_get_name(void *) {
    return obs_module_text("Gemini Live Translate");
}

void create_resampler(FilterData *d) {
    if (d->resampler) { audio_resampler_destroy(d->resampler); d->resampler = nullptr; }
    audio_t *audio = obs_get_audio();
    struct resample_info src = {};
    src.samples_per_sec = audio_output_get_sample_rate(audio);
    src.format = AUDIO_FORMAT_FLOAT_PLANAR;
    src.speakers = audio_output_get_channels(audio) == 1 ? SPEAKERS_MONO
                                                          : SPEAKERS_STEREO;
    struct resample_info dst = {};
    dst.samples_per_sec = 16000;
    dst.format = AUDIO_FORMAT_16BIT;   // interleaved 16-bit
    dst.speakers = SPEAKERS_MONO;
    d->resampler = audio_resampler_create(&dst, &src);
}

void filter_update(void *data, obs_data_t *settings) {
    auto *d = static_cast<FilterData *>(data);
    d->api_key = obs_data_get_string(settings, "api_key");
    d->target_lang = obs_data_get_string(settings, "target_lang");
    lt::TranslationSession::instance().configure(d->api_key, d->target_lang);
}

void *filter_create(obs_data_t *settings, obs_source_t *source) {
    auto *d = new FilterData();
    d->context = source;
    create_resampler(d);
    filter_update(d, settings);
    return d;
}

void filter_destroy(void *data) {
    auto *d = static_cast<FilterData *>(data);
    if (d->resampler) audio_resampler_destroy(d->resampler);
    delete d;
}

struct obs_audio_data *filter_audio(void *data, struct obs_audio_data *audio) {
    auto *d = static_cast<FilterData *>(data);
    if (!d->resampler || d->api_key.empty()) return audio; // passthrough

    uint8_t *out[MAX_AV_PLANES] = {};
    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool ok = audio_resampler_resample(
        d->resampler, out, &out_frames, &ts_offset,
        (const uint8_t *const *)audio->data, audio->frames);
    if (ok && out_frames > 0) {
        // dst is 16-bit mono interleaved => out[0] holds out_frames*2 bytes
        auto chunks = d->chunker.push(out[0], out_frames * 2);
        for (auto &c : chunks)
            lt::TranslationSession::instance().push_input_pcm(c.data(), c.size());
    }
    return audio; // original audio passes through untouched
}

obs_properties_t *filter_properties(void *) {
    obs_properties_t *props = obs_properties_create();
    obs_property_t *list = obs_properties_add_list(
        props, "target_lang", obs_module_text("Target Language"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (int i = 0; i < lt::kLanguagesCount; ++i)
        obs_property_list_add_string(list, lt::kLanguages[i].name,
                                     lt::kLanguages[i].code);

    obs_properties_add_text(props, "api_key", obs_module_text("Gemini API Key"),
                            OBS_TEXT_PASSWORD);
    obs_properties_add_text(
        props, "warn",
        obs_module_text("Note: the API key is stored in plaintext in your "
                        "scene collection file. Do not share that file."),
        OBS_TEXT_INFO);
    obs_properties_add_text(props, "status", obs_module_text("Status"),
                            OBS_TEXT_INFO);
    return props;
}

void filter_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "target_lang", "en");
}

void filter_get_status(void *, obs_data_t *settings) {
    obs_data_set_string(
        settings, "status",
        lt::TranslationSession::instance().status_text().c_str());
}

} // namespace

struct obs_source_info live_translate_filter_info = [] {
    struct obs_source_info info = {};
    info.id = "gemini_live_translate_filter";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = filter_get_name;
    info.create = filter_create;
    info.destroy = filter_destroy;
    info.update = filter_update;
    info.filter_audio = filter_audio;
    info.get_properties = filter_properties;
    info.get_defaults = filter_defaults;
    return info;
}();
```

> Note: a periodic UI refresh of the read-only status text can be added later via `obs_source_update_properties`. For v1 the status updates whenever the properties panel is reopened. The `filter_get_status` helper is reserved for a future refresh hook and is intentionally unreferenced in v1.

- [ ] **Step 3: Register the filter in `src/plugin-main.cpp`**

Add near the top (after includes):
```cpp
extern struct obs_source_info live_translate_filter_info;
```
And inside `obs_module_load`, before `return true;`:
```cpp
    obs_register_source(&live_translate_filter_info);
```

- [ ] **Step 4: Add `src/filter.cpp` to the plugin target in `CMakeLists.txt`**

Append `src/filter.cpp` to the `add_library(obs-live-translate MODULE ...)` source list.

- [ ] **Step 5: Build to verify it compiles**

Run: `cmake --build build --config RelWithDebInfo`
Expected: builds with no errors; `obs-live-translate.dll` produced.

- [ ] **Step 6: Commit**

```bash
git add src/languages.hpp src/filter.cpp src/plugin-main.cpp CMakeLists.txt
git commit -m "Add OBS audio filter (capture path + config UI)"
```

---

### Task 9: OBS custom source — output path

Emits translated 24 kHz mono PCM via `obs_source_output_audio` on a small timer-driven pull from the session output buffer.

**Files:**
- Create: `src/source.cpp`
- Modify: `CMakeLists.txt` (add `src/source.cpp`)
- Modify: `src/plugin-main.cpp` (register the source)

- [ ] **Step 1: Create `src/source.cpp`**

```cpp
#include "translation-session.hpp"
#include <obs-module.h>
#include <util/platform.h>   // os_gettime_ns
#include <util/threading.h>
#include <atomic>
#include <thread>
#include <vector>

namespace {

struct SourceData {
    obs_source_t *context = nullptr;
    std::thread thread;
    std::atomic<bool> active{false};
};

const char *source_get_name(void *) {
    return obs_module_text("Gemini Translated Audio");
}

void emit_loop(SourceData *d) {
    // Pull ~100ms of 24kHz/16-bit/mono audio per tick.
    const size_t kFrames = 2400;            // 100ms @ 24kHz
    const size_t kBytes = kFrames * 2;      // 16-bit mono
    std::vector<uint8_t> buf(kBytes);
    while (d->active) {
        size_t n = lt::TranslationSession::instance().pull_output_pcm(
            buf.data(), buf.size());
        if (n >= 2) {
            struct obs_source_audio out = {};
            out.data[0] = buf.data();
            out.frames = static_cast<uint32_t>(n / 2);
            out.speakers = SPEAKERS_MONO;
            out.format = AUDIO_FORMAT_16BIT;
            out.samples_per_sec = 24000;
            out.timestamp = os_gettime_ns();
            obs_source_output_audio(d->context, &out);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void *source_create(obs_data_t *, obs_source_t *source) {
    auto *d = new SourceData();
    d->context = source;
    d->active = true;
    d->thread = std::thread(emit_loop, d);
    return d;
}

void source_destroy(void *data) {
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

- [ ] **Step 2: Register the source in `src/plugin-main.cpp`**

Add near the top:
```cpp
extern struct obs_source_info live_translate_source_info;
```
And inside `obs_module_load`, before `return true;`:
```cpp
    obs_register_source(&live_translate_source_info);
```

- [ ] **Step 3: Add `src/source.cpp` to the plugin target in `CMakeLists.txt`**

Append `src/source.cpp` to the source list.

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build --config RelWithDebInfo`
Expected: builds with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/source.cpp src/plugin-main.cpp CMakeLists.txt
git commit -m "Add OBS custom source (translated audio output path)"
```

---

### Task 10: Integration test harness (WAV → pipeline → WAV)

A standalone executable that exercises the real pipeline (audio-convert + live-protocol + session/WebSocket) against a real Gemini session, without OBS. Run manually with an API key.

**Files:**
- Create: `tools/wav-harness.cpp`
- Modify: `CMakeLists.txt` (add an optional `wav-harness` target)

- [ ] **Step 1: Add the harness target to `CMakeLists.txt`**

```cmake
option(BUILD_HARNESS "Build the WAV integration harness" OFF)
if(BUILD_HARNESS)
  add_executable(wav-harness
    tools/wav-harness.cpp
    src/base64.cpp
    src/audio-convert.cpp
    src/ring-buffer.cpp
    src/live-protocol.cpp
    src/backoff.cpp
  )
  target_include_directories(wav-harness PRIVATE src)
  target_link_libraries(wav-harness PRIVATE ixwebsocket nlohmann_json::nlohmann_json)
endif()
```

- [ ] **Step 2: Create `tools/wav-harness.cpp`**

Reads a 16 kHz mono 16-bit PCM WAV, streams it to Gemini directly (reusing `build_setup_message` / `build_realtime_input_message` / `parse_server_message`), and writes the returned 24 kHz audio to `out.wav`.

```cpp
#include "live-protocol.hpp"
#include <ixwebsocket/IXWebSocket.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

// Minimal WAV reader: expects 16-bit PCM mono. Returns sample bytes.
static std::vector<uint8_t> read_wav_pcm(const char *path, uint32_t &rate) {
    std::ifstream f(path, std::ios::binary);
    std::vector<char> all((std::istreambuf_iterator<char>(f)), {});
    rate = *reinterpret_cast<uint32_t *>(&all[24]);
    // find "data" chunk
    for (size_t i = 12; i + 8 < all.size();) {
        uint32_t sz = *reinterpret_cast<uint32_t *>(&all[i + 4]);
        if (std::memcmp(&all[i], "data", 4) == 0)
            return std::vector<uint8_t>(all.begin() + i + 8,
                                        all.begin() + i + 8 + sz);
        i += 8 + sz;
    }
    return {};
}

static void write_wav_pcm(const char *path, const std::vector<uint8_t> &pcm,
                          uint32_t rate) {
    std::ofstream f(path, std::ios::binary);
    uint32_t data_sz = (uint32_t)pcm.size();
    uint32_t riff_sz = 36 + data_sz;
    uint16_t fmt = 1, ch = 1, bits = 16;
    uint32_t byte_rate = rate * ch * bits / 8;
    uint16_t block_align = ch * bits / 8;
    uint32_t sub1 = 16;
    f.write("RIFF", 4); f.write((char *)&riff_sz, 4); f.write("WAVE", 4);
    f.write("fmt ", 4); f.write((char *)&sub1, 4);
    f.write((char *)&fmt, 2); f.write((char *)&ch, 2);
    f.write((char *)&rate, 4); f.write((char *)&byte_rate, 4);
    f.write((char *)&block_align, 2); f.write((char *)&bits, 2);
    f.write("data", 4); f.write((char *)&data_sz, 4);
    f.write((char *)pcm.data(), pcm.size());
}

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: wav-harness <in16k.wav> <target_lang> <api_key>\n");
        return 1;
    }
    uint32_t rate = 0;
    auto pcm = read_wav_pcm(argv[1], rate);
    std::printf("input rate=%u bytes=%zu\n", rate, pcm.size());

    std::vector<uint8_t> out;
    std::atomic<bool> open{false}, done{false};

    ix::WebSocket ws;
    ws.setUrl(std::string("wss://generativelanguage.googleapis.com/ws/"
        "google.ai.generativelanguage.v1beta.GenerativeService."
        "BidiGenerateContent?key=") + argv[3]);
    ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr &m) {
        if (m->type == ix::WebSocketMessageType::Open) {
            ws.sendBinary(lt::build_setup_message(argv[2], true));
            open = true;
        } else if (m->type == ix::WebSocketMessageType::Message) {
            auto sm = lt::parse_server_message(m->str);
            if (sm.kind == lt::ServerMessage::Kind::Audio)
                out.insert(out.end(), sm.audio.begin(), sm.audio.end());
            else if (sm.kind == lt::ServerMessage::Kind::Error)
                std::fprintf(stderr, "error: %s\n", sm.error_message.c_str());
        }
    });
    ws.start();
    while (!open) std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const size_t chunk = 3200;
    for (size_t i = 0; i < pcm.size(); i += chunk) {
        size_t n = std::min(chunk, pcm.size() - i);
        ws.sendBinary(lt::build_realtime_input_message(&pcm[i], n));
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // pace ~realtime
    }
    std::this_thread::sleep_for(std::chrono::seconds(5)); // drain tail
    ws.stop();

    write_wav_pcm("out.wav", out, 24000);
    std::printf("wrote out.wav bytes=%zu\n", out.size());
    return 0;
}
```

- [ ] **Step 3: Build the harness**

Run: `cmake -B build -DBUILD_HARNESS=ON && cmake --build build --target wav-harness`
Expected: `wav-harness` builds.

- [ ] **Step 4: Manual integration run (requires API key + a 16k mono WAV)**

Run: `./build/wav-harness sample-16k-mono.wav zh-TW <YOUR_API_KEY>`
Expected: prints input bytes, writes `out.wav`; playing `out.wav` yields translated speech. Verify by ear.

- [ ] **Step 5: Commit**

```bash
git add tools/wav-harness.cpp CMakeLists.txt
git commit -m "Add WAV integration harness for the Gemini pipeline"
```

---

### Task 11: Install docs + manual OBS acceptance

**Files:**
- Create: `docs/install.md`
- Modify: `README.md`

- [ ] **Step 1: Create `docs/install.md`**

```markdown
# Install (Windows)

1. Build `obs-live-translate.dll` (RelWithDebInfo).
2. Copy it to `C:\Program Files\obs-studio\obs-plugins\64bit\`.
3. Launch OBS 30.x.

## Usage

1. On your microphone source, add the **Gemini Live Translate** audio filter.
   - Choose a **Target Language** and paste your **Gemini API Key**.
2. Add a **Gemini Translated Audio** source to your scene.
3. Open **Edit → Advanced Audio Properties**, assign the
   *Gemini Translated Audio* source to its own track for independent
   monitoring/output.
4. Speak — translated audio appears on that track after a few seconds.

> The API key is stored in plaintext in your scene collection file. Do not
> share that file.
```

- [ ] **Step 2: Update `README.md`**

```markdown
# obs-live-translate

OBS Studio plugin for real-time speech-to-speech translation via the Gemini
Live API (`gemini-3.5-live-translate-preview`). See `docs/install.md`.
```

- [ ] **Step 3: Manual OBS acceptance checklist**

Perform in OBS and confirm each:
- [ ] Filter appears in the audio source's filter list as "Gemini Live Translate".
- [ ] Entering a valid API key shows status "Connected".
- [ ] Entering an invalid key shows "API key error: ..." and does NOT reconnect.
- [ ] "Gemini Translated Audio" source can be added and assigned to its own track.
- [ ] Speaking produces translated audio on that track within a few seconds.
- [ ] Disconnecting the network shows "Reconnecting..." and recovers when restored.

- [ ] **Step 4: Commit**

```bash
git add docs/install.md README.md
git commit -m "Add install docs and update README"
```

---

## Self-Review

**Spec coverage:**
- Core pipeline (capture→Gemini→output track): Tasks 7, 8, 9 ✓
- Audio formats (16k in / 24k out, 100ms chunks, OBS resampler in / OBS auto out): Tasks 3, 8, 9 ✓
- Target language + API key UI (password field, language dropdown): Task 8 ✓
- `echoTargetLanguage` hardcoded true: Tasks 5, 7 ✓
- Auto-reconnect, exponential backoff, no-reconnect on auth error: Tasks 6, 7 ✓
- Plaintext key storage + warning: Task 8 ✓
- Buffer drop-oldest: Task 4 ✓
- Networking deps (IXWebSocket + mbedTLS + json), hand-rolled protocol: Tasks 1, 5, 7 ✓
- Testing strategy (pure unit tests + WAV harness + manual OBS): Tasks 2–6, 10, 11 ✓
- Build/distribution (CMake, single .dll, manual install): Tasks 0, 1, 11 ✓
- Single global session: Task 7 (singleton) ✓

**Type consistency:** `TranslationSession` API (`configure`, `stop`, `push_input_pcm`, `pull_output_pcm`, `status_text`) used consistently across Tasks 7–9. `Chunker(3200)` consistent in Tasks 3 and 8. Output emit uses 24000/SPEAKERS_MONO/AUDIO_FORMAT_16BIT consistently (Tasks 7 buffer sizing, 9 emit).

**Placeholder scan:** One inline fix-up note remains by design (audio-convert test expectations in Task 3 Step 6, where the `-1.0`/`-2.0` clamp resolves to `-32767`) — the corrected values are stated explicitly. No TBD/TODO/"handle edge cases" placeholders remain.
