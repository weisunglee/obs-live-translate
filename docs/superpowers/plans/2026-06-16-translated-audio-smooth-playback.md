# Translated Audio Smooth Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make translated audio favor smooth sentence endings by using a larger startup buffer and a grace window before stopping playback.

**Architecture:** Replace the current boolean output jitter helper with a small pure playback state machine. The OBS source loop asks the state machine for a per-tick decision, pulls audio only when the decision says to emit real audio, and lets the existing PCM smoother fade only after the grace window expires.

**Tech Stack:** C++17, OBS native plugin APIs, Catch2 unit tests, CMake/CTest on Windows.

---

## File Structure

- Modify `src/audio-pacing.hpp`: define `OutputPlaybackAction` and update `OutputJitterBuffer` to track priming, playing, and grace timing.
- Modify `src/audio-pacing.cpp`: implement 1200 ms startup buffering, 500 ms grace behavior, and helper constants.
- Modify `tests/test-audio-pacing.cpp`: replace old boolean jitter tests with state-machine tests from the design.
- Modify `src/source.cpp`: pass elapsed tick time into the jitter helper and only call `pull_output_pcm` for real-audio playback decisions.

## Environment Commands

Use these commands from `C:\Users\ialkk\source\repos\obs-live-translate`.

Configure PATH before CMake or CTest:

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
```

Build unit tests:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build_x64 --config RelWithDebInfo --target unit-tests
```

Run audio pacing tests:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build_x64 -C RelWithDebInfo -R audio
```

Run all tests:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

---

### Task 1: Define Smooth Playback Policy With Failing Tests

**Files:**
- Modify: `tests/test-audio-pacing.cpp`
- Modify: `src/audio-pacing.hpp`

- [ ] **Step 1: Write the failing tests**

Replace the existing `OutputJitterBuffer` tests in `tests/test-audio-pacing.cpp` with these tests. Keep the existing packet duration and PCM smoother tests unchanged.

```cpp
TEST_CASE("default output jitter thresholds favor smooth translated speech")
{
    REQUIRE(output_jitter_start_bytes() == audio_packet_shape(24000, 16, 1, 1200).bytes);
    REQUIRE(output_jitter_grace_ms() == 500);
}

TEST_CASE("output jitter waits for startup threshold before playback")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(start_threshold - 1, 960, 20, false) ==
            OutputPlaybackAction::Silence);
    REQUIRE(jitter.next_action(start_threshold, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
}

TEST_CASE("output jitter keeps playing below startup threshold after start")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(start_threshold, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
    REQUIRE(jitter.next_action(audio_packet_shape(24000, 16, 1, 200).bytes, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
}

TEST_CASE("output jitter enters grace instead of stopping immediately")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(start_threshold, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
    REQUIRE(jitter.next_action(0, 960, 20, false) ==
            OutputPlaybackAction::Hold);
    REQUIRE(jitter.next_action(0, 960, 480, false) ==
            OutputPlaybackAction::Hold);
    REQUIRE(jitter.next_action(0, 960, 20, false) ==
            OutputPlaybackAction::Silence);
}

TEST_CASE("output jitter resumes playback when audio arrives during grace")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(start_threshold, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
    REQUIRE(jitter.next_action(0, 960, 20, false) ==
            OutputPlaybackAction::Hold);
    REQUIRE(jitter.next_action(960, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
    REQUIRE(jitter.next_action(960, 960, 20, false) ==
            OutputPlaybackAction::PlayAudio);
}

TEST_CASE("output jitter can drain a tail after input idle while priming")
{
    size_t start_threshold = output_jitter_start_bytes();
    OutputJitterBuffer jitter(start_threshold, output_jitter_grace_ms());
    REQUIRE(jitter.next_action(960, 960, 20, false) ==
            OutputPlaybackAction::Silence);
    REQUIRE(jitter.next_action(960, 960, 20, true) ==
            OutputPlaybackAction::PlayAudio);
}
```

Update `src/audio-pacing.hpp` so the tests compile but fail until implementation is complete:

```cpp
enum class OutputPlaybackAction {
    Silence,
    PlayAudio,
    Hold,
};

size_t output_jitter_grace_ms();

class OutputJitterBuffer {
public:
    OutputJitterBuffer(size_t start_threshold_bytes, size_t grace_ms);
    OutputPlaybackAction next_action(size_t buffered_bytes, size_t packet_bytes,
                                     uint32_t elapsed_ms,
                                     bool input_idle_flush);

private:
    enum class State {
        Priming,
        Playing,
        Grace,
    };

    size_t start_threshold_bytes_;
    size_t grace_ms_;
    State state_ = State::Priming;
    uint32_t grace_elapsed_ms_ = 0;
};
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build_x64 --config RelWithDebInfo --target unit-tests
```

Expected: build fails because `output_jitter_grace_ms` and `OutputJitterBuffer::next_action` are declared but not implemented, or because `source.cpp` still uses the removed constructor/API.

- [ ] **Step 3: Commit is not done in this task**

Do not commit after the failing-test-only state. Continue to Task 2 to make tests pass.

---

### Task 2: Implement Playback State Machine

**Files:**
- Modify: `src/audio-pacing.cpp`
- Modify: `src/audio-pacing.hpp`
- Test: `tests/test-audio-pacing.cpp`

- [ ] **Step 1: Write minimal implementation**

In `src/audio-pacing.cpp`, change the jitter constants and implementation:

```cpp
size_t output_jitter_start_bytes()
{
    return audio_packet_shape(24000, 16, 1, 1200).bytes;
}

size_t output_jitter_grace_ms()
{
    return 500;
}

OutputJitterBuffer::OutputJitterBuffer(size_t start_threshold_bytes,
                                       size_t grace_ms)
    : start_threshold_bytes_(start_threshold_bytes), grace_ms_(grace_ms)
{}

OutputPlaybackAction OutputJitterBuffer::next_action(size_t buffered_bytes,
                                                     size_t packet_bytes,
                                                     uint32_t elapsed_ms,
                                                     bool input_idle_flush)
{
    if (buffered_bytes >= packet_bytes) {
        if (state_ == State::Priming &&
            buffered_bytes < start_threshold_bytes_ && !input_idle_flush) {
            return OutputPlaybackAction::Silence;
        }
        state_ = State::Playing;
        grace_elapsed_ms_ = 0;
        return OutputPlaybackAction::PlayAudio;
    }

    if (state_ == State::Playing || state_ == State::Grace) {
        state_ = State::Grace;
        grace_elapsed_ms_ += elapsed_ms;
        if (grace_elapsed_ms_ <= grace_ms_)
            return OutputPlaybackAction::Hold;
    }

    state_ = State::Priming;
    grace_elapsed_ms_ = 0;
    return OutputPlaybackAction::Silence;
}
```

Remove `output_jitter_min_bytes()` from the header and implementation if no call sites remain after Task 3. If it still has call sites before Task 3, leave it until Task 3.

- [ ] **Step 2: Run audio pacing tests**

Run:

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build_x64 --config RelWithDebInfo --target unit-tests
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build_x64 -C RelWithDebInfo -R audio --output-on-failure
```

Expected: the build may still fail because `src/source.cpp` has not been updated to the new constructor/API. If it fails for `source.cpp`, continue to Task 3. If the build succeeds, the audio pacing tests should pass.

- [ ] **Step 3: Commit is not done in this task**

Do not commit until the OBS source loop is updated and the full build is green.

---

### Task 3: Wire Source Loop To Playback Actions

**Files:**
- Modify: `src/source.cpp`
- Modify: `src/audio-pacing.hpp`
- Modify: `src/audio-pacing.cpp`
- Test: `tests/test-audio-pacing.cpp`

- [ ] **Step 1: Update source data construction**

In `src/source.cpp`, change the jitter member initialization:

```cpp
lt::OutputJitterBuffer jitter{lt::output_jitter_start_bytes(),
                              lt::output_jitter_grace_ms()};
```

- [ ] **Step 2: Update the emit loop action handling**

Replace the play/stop section in `emit_loop` with:

```cpp
size_t buffered = session.output_buffered_bytes();
bool input_idle_flush = session.input_idle_ms() >= 700;
lt::OutputPlaybackAction action =
    d->jitter.next_action(buffered, buf.size(), 20, input_idle_flush);
bool has_audio = action == lt::OutputPlaybackAction::PlayAudio;
if (has_audio)
    session.pull_output_pcm(buf.data(), buf.size());
bool playing = action != lt::OutputPlaybackAction::Silence;
if (playing != d->was_playing) {
    blog(LOG_INFO, "[live-translate] output %s buffered=%zu",
         playing ? "resumed" : "stopped", buffered);
    d->was_playing = playing;
}
d->smoother.apply(reinterpret_cast<int16_t *>(buf.data()), packet.frames,
                  has_audio);
```

This deliberately treats `Hold` as an active playback run for logging, but sends silence through the smoother with `has_audio == false`. That means the first no-audio tick fades out instead of hard cutting, and late Gemini audio during the 500 ms grace window resumes without forcing the state machine back to startup buffering.

- [ ] **Step 3: Remove obsolete min threshold helper**

If no references remain, delete `output_jitter_min_bytes()` from `src/audio-pacing.hpp` and `src/audio-pacing.cpp`. Confirm with:

```powershell
rg -n "output_jitter_min_bytes|should_play" src tests
```

Expected: no matches.

- [ ] **Step 4: Build and run all tests**

Run:

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build_x64 --config RelWithDebInfo --target unit-tests
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

Expected: all unit tests pass.

- [ ] **Step 5: Commit**

Run:

```powershell
git add src\audio-pacing.hpp src\audio-pacing.cpp src\source.cpp tests\test-audio-pacing.cpp
git commit -m "Smooth translated audio playback tails"
```

Expected: commit succeeds.

---

### Task 4: Build Plugin DLL And Install For OBS Testing

**Files:**
- Build artifact: `build_x64\RelWithDebInfo\obs-live-translate.dll`
- Installed artifact: `C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-translate.dll`

- [ ] **Step 1: Build the plugin DLL**

Run:

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build_x64 --config RelWithDebInfo
```

Expected: `obs-live-translate.dll` is produced in `build_x64\RelWithDebInfo`.

- [ ] **Step 2: Run CTest**

Run:

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 3: Install the DLL**

Use an elevated PowerShell copy if normal copy is denied:

```powershell
Copy-Item -LiteralPath build_x64\RelWithDebInfo\obs-live-translate.dll -Destination 'C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-translate.dll' -Force
Get-FileHash build_x64\RelWithDebInfo\obs-live-translate.dll
Get-FileHash 'C:\Program Files\obs-studio\obs-plugins\64bit\obs-live-translate.dll'
```

Expected: source and destination hashes match.

- [ ] **Step 4: Manual OBS verification**

Ask the user to restart OBS and record this sequence:

1. Say one Chinese sentence.
2. Stop speaking for 2-3 seconds.
3. Stop recording and listen to the ending.
4. Record another short unrelated sentence and confirm no previous translated tail appears at the start.

Expected: translated speech has more startup delay but sentence endings should be smoother, with no late tail appearing only after another spoken segment.
