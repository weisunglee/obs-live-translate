# Single-session guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the silent breakage when a user adds a second *Gemini Live Translate* filter or a second *Gemini Translated Audio* source — make the first instance the active one (first-wins) and disable extras with a visible warning.

**Architecture:** Introduce a small, pure `OwnerGuard` class (mutex + a single opaque owner pointer) that grants ownership only when unowned. `TranslationSession` holds two guards — one for the input stream (filters), one for the output stream (sources) — and exposes claim/release/query wrappers. The filter and source each claim their guard with their own data pointer as the token; non-owners become passive (pass-through / silent) and surface a warning in their properties' `status` field. A removed owner releases its guard, and the surviving instance picks it up on its next audio/loop iteration.

**Tech Stack:** C++17, OBS plugin API (libobs), Catch2 v3 unit tests, CMake. Builds and tests run on the **Windows box over SSH** (the mac has no libobs); use the `build_x64` tree as in the README.

---

## File structure

- **Create** `src/owner-guard.hpp` / `src/owner-guard.cpp` — the `OwnerGuard` first-wins single-owner primitive. Pure (only `<mutex>`), so it is unit-testable without libobs.
- **Create** `tests/test-owner-guard.cpp` — Catch2 tests for `OwnerGuard`.
- **Modify** `tests/CMakeLists.txt` — add the test file and `src/owner-guard.cpp` to the `unit-tests` target.
- **Modify** `CMakeLists.txt` — add `src/owner-guard.cpp` to the plugin MODULE sources.
- **Modify** `src/translation-session.hpp` / `src/translation-session.cpp` — hold two `OwnerGuard`s and expose `claim_input/release_input/input_owned_by_other` and the `_output` equivalents; `release_input` stops the session when no filter owns input.
- **Modify** `src/filter.cpp` — claim input ownership (first-wins); non-owners pass audio through untouched and show a warning.
- **Modify** `src/source.cpp` — claim output ownership; non-owners stay silent (don't drain the buffer) and show a warning via a new minimal properties panel.
- **Modify** `README.md` — document the single-filter / single-source limitation.

---

## Task 1: `OwnerGuard` primitive (TDD)

**Files:**
- Create: `tests/test-owner-guard.cpp`
- Create: `src/owner-guard.hpp`, `src/owner-guard.cpp`
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test-owner-guard.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "owner-guard.hpp"

using lt::OwnerGuard;

TEST_CASE("first claimer wins, second is denied")
{
    OwnerGuard g;
    int a, b; // distinct objects -> distinct token addresses
    REQUIRE(g.claim(&a));
    REQUIRE_FALSE(g.claim(&b));
    REQUIRE(g.owned_by_other(&b));
    REQUIRE_FALSE(g.owned_by_other(&a));
    REQUIRE(g.has_owner());
}

TEST_CASE("claiming again with the same token stays owned")
{
    OwnerGuard g;
    int a;
    REQUIRE(g.claim(&a));
    REQUIRE(g.claim(&a));
    REQUIRE_FALSE(g.owned_by_other(&a));
}

TEST_CASE("release frees the resource for another token")
{
    OwnerGuard g;
    int a, b;
    REQUIRE(g.claim(&a));
    g.release(&a);
    REQUIRE_FALSE(g.has_owner());
    REQUIRE(g.claim(&b));
    REQUIRE(g.owned_by_other(&a));
}

TEST_CASE("a non-owner cannot release")
{
    OwnerGuard g;
    int a, b;
    REQUIRE(g.claim(&a));
    g.release(&b); // no-op: b is not the owner
    REQUIRE(g.has_owner());
    REQUIRE_FALSE(g.claim(&b));
}
```

- [ ] **Step 2: Wire the test and source into the unit-tests target**

In `tests/CMakeLists.txt`, add `test-owner-guard.cpp` to the test sources and `${CMAKE_SOURCE_DIR}/src/owner-guard.cpp` to the compiled sources. The `add_executable(unit-tests ...)` block becomes:

```cmake
add_executable(unit-tests
  test-main.cpp
  test-audio-convert.cpp
  test-audio-pacing.cpp
  test-base64.cpp
  test-backoff.cpp
  test-live-protocol.cpp
  test-ring-buffer.cpp
  test-owner-guard.cpp
  test-smoke.cpp
  ${CMAKE_SOURCE_DIR}/src/audio-convert.cpp
  ${CMAKE_SOURCE_DIR}/src/audio-pacing.cpp
  ${CMAKE_SOURCE_DIR}/src/backoff.cpp
  ${CMAKE_SOURCE_DIR}/src/base64.cpp
  ${CMAKE_SOURCE_DIR}/src/live-protocol.cpp
  ${CMAKE_SOURCE_DIR}/src/owner-guard.cpp
  ${CMAKE_SOURCE_DIR}/src/ring-buffer.cpp
)
```

- [ ] **Step 3: Run the test to verify it fails**

On the Windows box (SSH):

```powershell
cmake --preset windows-x64 -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
cmake --build --preset windows-x64 --target unit-tests
```

Expected: build FAILS — `owner-guard.hpp` does not exist (`cannot open source file "owner-guard.hpp"`).

- [ ] **Step 4: Create the header**

Create `src/owner-guard.hpp`:

```cpp
#pragma once
#include <mutex>

namespace lt {

// First-wins single-owner guard. The token is an opaque pointer identifying the
// claimant (e.g. a per-instance data pointer). Ownership is granted only when
// the resource is unowned, so the first successful claimer keeps it until it
// releases. Used to keep a single filter owning the shared input stream and a
// single source owning the shared output stream.
class OwnerGuard {
public:
    // Take ownership iff currently unowned (or already held by token).
    // Returns true iff token owns the resource after the call.
    bool claim(const void *token);
    // Clear ownership, but only if token is the current holder (else no-op).
    void release(const void *token);
    // True iff some token other than `token` currently owns the resource.
    bool owned_by_other(const void *token);
    // True iff any token currently owns the resource.
    bool has_owner();

private:
    std::mutex mtx_;
    const void *owner_ = nullptr;
};

}
```

- [ ] **Step 5: Create the implementation**

Create `src/owner-guard.cpp`:

```cpp
#include "owner-guard.hpp"

namespace lt {

bool OwnerGuard::claim(const void *token)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (owner_ == nullptr)
        owner_ = token;
    return owner_ == token;
}

void OwnerGuard::release(const void *token)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (owner_ == token)
        owner_ = nullptr;
}

bool OwnerGuard::owned_by_other(const void *token)
{
    std::lock_guard<std::mutex> lk(mtx_);
    return owner_ != nullptr && owner_ != token;
}

bool OwnerGuard::has_owner()
{
    std::lock_guard<std::mutex> lk(mtx_);
    return owner_ != nullptr;
}

}
```

- [ ] **Step 6: Add the source to the plugin build**

In `CMakeLists.txt`, add `src/owner-guard.cpp` to the `add_library(${CMAKE_PROJECT_NAME} MODULE ...)` list (e.g. right after `src/backoff.cpp`):

```cmake
add_library(${CMAKE_PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/base64.cpp
  src/audio-convert.cpp
  src/audio-pacing.cpp
  src/filter.cpp
  src/ring-buffer.cpp
  src/live-protocol.cpp
  src/backoff.cpp
  src/owner-guard.cpp
  src/source.cpp
  src/translation-session.cpp
)
```

- [ ] **Step 7: Run the test to verify it passes**

```powershell
cmake --build --preset windows-x64 --target unit-tests
ctest --test-dir build_x64 -R owner -V
```

Expected: build succeeds; the four `OwnerGuard` cases PASS, plus the existing suite still builds.

- [ ] **Step 8: Commit**

```bash
git add src/owner-guard.hpp src/owner-guard.cpp tests/test-owner-guard.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "Add OwnerGuard first-wins single-owner primitive with tests"
```

---

## Task 2: Wire two ownership guards into `TranslationSession`

**Files:**
- Modify: `src/translation-session.hpp`, `src/translation-session.cpp`

- [ ] **Step 1: Declare the guards and wrapper API**

In `src/translation-session.hpp`, add the include near the top:

```cpp
#include "owner-guard.hpp"
```

Add these public methods (after `uint64_t input_idle_ms();`):

```cpp
    // Single-owner guards: only one filter may own the input stream and only
    // one source may own the output stream at a time (first-wins). Extra
    // instances are disabled so two of either don't corrupt the shared session.
    bool claim_input(const void *token);
    void release_input(const void *token);
    bool input_owned_by_other(const void *token);
    bool claim_output(const void *token);
    void release_output(const void *token);
    bool output_owned_by_other(const void *token);
```

Add these private members (e.g. after `bool echo_target_ = true;`):

```cpp
    OwnerGuard input_owner_;
    OwnerGuard output_owner_;
```

- [ ] **Step 2: Implement the wrappers**

In `src/translation-session.cpp`, add these definitions (e.g. right after the `configure(...)` definition):

```cpp
bool TranslationSession::claim_input(const void *token)
{
    return input_owner_.claim(token);
}

void TranslationSession::release_input(const void *token)
{
    input_owner_.release(token);
    // No filter owns the input stream anymore: tear down the WebSocket so we
    // don't stay connected with nothing to translate. The next filter to claim
    // input restarts it via configure().
    if (!input_owner_.has_owner())
        stop();
}

bool TranslationSession::input_owned_by_other(const void *token)
{
    return input_owner_.owned_by_other(token);
}

bool TranslationSession::claim_output(const void *token)
{
    return output_owner_.claim(token);
}

void TranslationSession::release_output(const void *token)
{
    output_owner_.release(token);
}

bool TranslationSession::output_owned_by_other(const void *token)
{
    return output_owner_.owned_by_other(token);
}
```

- [ ] **Step 3: Build to verify it compiles**

```powershell
cmake --build --preset windows-x64
```

Expected: the plugin DLL builds with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/translation-session.hpp src/translation-session.cpp
git commit -m "Add input/output ownership guards to TranslationSession"
```

---

## Task 3: Guard the filter (first-wins input ownership + warning)

**Files:**
- Modify: `src/filter.cpp`

- [ ] **Step 1: Add an `active` flag to the filter state**

In `src/filter.cpp`, add a field to `struct FilterData` (after `bool echo_target = true;`):

```cpp
    bool active = false; // true iff this filter currently owns the input stream
```

- [ ] **Step 2: Claim ownership in `filter_update`**

Replace the body of `filter_update` with:

```cpp
void filter_update(void *data, obs_data_t *settings)
{
    auto *d = static_cast<FilterData *>(data);
    d->api_key = obs_data_get_string(settings, "api_key");
    d->target_lang = obs_data_get_string(settings, "target_lang");
    d->echo_target = obs_data_get_bool(settings, "echo_target");

    auto &session = lt::TranslationSession::instance();
    if (d->api_key.empty()) {
        // Nothing to run. If we were the owner, release so another filter can
        // take over.
        if (d->active) {
            session.release_input(d);
            d->active = false;
        }
        return;
    }
    // First filter to claim the shared session wins; extras stay disabled.
    d->active = session.claim_input(d);
    if (d->active)
        session.configure(d->api_key, d->target_lang, d->echo_target);
}
```

- [ ] **Step 3: Make non-owners passive in `filter_audio`**

In `filter_audio`, immediately after the existing guard line
`if (!d->resampler || d->api_key.empty()) return audio;`, insert:

```cpp
    auto &session = lt::TranslationSession::instance();
    // Claim/keep ownership. If another filter owns the session we stay a passive
    // pass-through; when the previous owner goes away we pick it up here and
    // (re)apply our config on the inactive->active transition.
    bool owner = session.claim_input(d);
    if (owner != d->active) {
        d->active = owner;
        if (owner)
            session.configure(d->api_key, d->target_lang, d->echo_target);
    }
    if (!owner) return audio;
```

Then change the existing push call to go through the local `session` reference (replace the `lt::TranslationSession::instance().push_input_pcm(...)` line inside the chunk loop):

```cpp
        auto chunks = d->chunker.push(out[0], out_frames * 2);
        for (auto &c : chunks)
            session.push_input_pcm(c.data(), c.size());
```

- [ ] **Step 4: Release ownership in `filter_destroy`**

Replace the body of `filter_destroy` with:

```cpp
void filter_destroy(void *data)
{
    auto *d = static_cast<FilterData *>(data);
    lt::TranslationSession::instance().release_input(d);
    if (d->resampler) audio_resampler_destroy(d->resampler);
    delete d;
}
```

- [ ] **Step 5: Show the warning in `filter_properties`**

Change the signature `obs_properties_t *filter_properties(void *)` to `obs_properties_t *filter_properties(void *data)`, and replace the existing `status` field block (the final `obs_properties_add_text(props, "status", ...)` before `return props;`) with:

```cpp
    auto *d = static_cast<FilterData *>(data);
    std::string status =
        (d && lt::TranslationSession::instance().input_owned_by_other(d))
            ? obs_module_text("Another Gemini Live Translate filter is already "
                              "active; this one is disabled.")
            : lt::TranslationSession::instance().status_text();
    obs_properties_add_text(props, "status", status.c_str(), OBS_TEXT_INFO);
```

(`status` is a `std::string` held in a local so its buffer outlives the
`obs_properties_add_text` call; `<string>` is already included.)

- [ ] **Step 6: Build to verify it compiles**

```powershell
cmake --build --preset windows-x64
```

Expected: builds with no errors.

- [ ] **Step 7: Commit**

```bash
git add src/filter.cpp
git commit -m "Guard the filter: first-wins input ownership, disable + warn extras"
```

---

## Task 4: Guard the source (first-wins output ownership + warning)

**Files:**
- Modify: `src/source.cpp`

- [ ] **Step 1: Add includes and an ownership flag**

In `src/source.cpp`, add `#include <string>` with the other includes. Add a field to `struct SourceData` (after `std::atomic<bool> active{false};`):

```cpp
    std::atomic<bool> owns_output{false}; // true iff this source owns the output
```

- [ ] **Step 2: Claim output ownership in `push_loop`; stay silent otherwise**

Replace the body of `push_loop` with:

```cpp
void push_loop(SourceData *d)
{
    lt::TranslationSession &session = lt::TranslationSession::instance();
    std::vector<uint8_t> buf(kDrainCapBytes);
    while (d->active) {
        // First source to claim the shared output wins; extras stay silent so
        // they don't steal chunks from the active one (reads are consuming).
        bool owner = session.claim_output(d);
        d->owns_output = owner;
        if (!owner) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kWaitTimeoutMs));
            continue;
        }

        if (session.take_interrupted())
            d->timestamper.reset();

        size_t n = session.wait_and_read_output(buf.data(), buf.size(),
                                                kWaitTimeoutMs);
        if (n < sizeof(int16_t))
            continue;
        size_t frames = n / sizeof(int16_t);

        uint64_t now = os_gettime_ns();
        uint64_t ts = d->timestamper.next_timestamp(now, frames);
        uint64_t delay = d->timestamper.pacing_delay_ns(now);
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::nanoseconds(delay));

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
```

- [ ] **Step 3: Release output ownership in `source_destroy`**

Replace the body of `source_destroy` with:

```cpp
void source_destroy(void *data)
{
    auto *d = static_cast<SourceData *>(data);
    d->active = false;
    if (d->thread.joinable()) d->thread.join();
    lt::TranslationSession::instance().release_output(d);
    delete d;
}
```

- [ ] **Step 4: Add a properties panel that shows the warning**

Add this function inside the anonymous namespace (e.g. after `source_get_name`):

```cpp
obs_properties_t *source_properties(void *data)
{
    obs_properties_t *props = obs_properties_create();
    auto *d = static_cast<SourceData *>(data);
    std::string status =
        (d && lt::TranslationSession::instance().output_owned_by_other(d))
            ? obs_module_text("Another Gemini Translated Audio source is "
                              "already active; this one is muted.")
            : obs_module_text("Active");
    obs_properties_add_text(props, "status", status.c_str(), OBS_TEXT_INFO);
    return props;
}
```

Then wire it into the source info by adding one line in the
`live_translate_source_info` initializer (after `info.destroy = source_destroy;`):

```cpp
    info.get_properties = source_properties;
```

- [ ] **Step 5: Build to verify it compiles**

```powershell
cmake --build --preset windows-x64
```

Expected: builds with no errors.

- [ ] **Step 6: Commit**

```bash
git add src/source.cpp
git commit -m "Guard the source: first-wins output ownership, mute + warn extras"
```

---

## Task 5: Manual verification + document the limitation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Deploy the freshly built DLL and run the manual checks**

Overwrite the deployed `obs-live-translate.dll` (Windows box, OBS closed), then in OBS:

1. **Filter, first-wins + warning:** add the *Gemini Live Translate* filter to a mic with an API key + target language → it connects and translates. Add a **second** filter (different language) to the same (or another) source. Expected: audio is **not** garbled; the second filter's properties `status` shows "Another Gemini Live Translate filter is already active; this one is disabled."
2. **Filter takeover:** remove the first filter. Expected: within a moment the second filter takes over — its translation (its language) starts working.
3. **Source, first-wins + warning:** add a *Gemini Translated Audio* source → it plays the translated voice. Add a **second** source. Expected: the second is silent (the first keeps playing cleanly, not split/choppy); the second source's properties `status` shows "Another Gemini Translated Audio source is already active; this one is muted."
4. **Source takeover:** remove the first source. Expected: the second source starts playing.

- [ ] **Step 2: Document the limitation in the README**

In `README.md`, under the `## Status` list, add a bullet:

```markdown
- ✅ Single-session by design: one active filter and one active translated-audio
  source. If you add a second of either, the first keeps working and the extra
  is disabled (filter) / muted (source) with a warning in its properties;
  removing the active one lets the extra take over.
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "Document single-session (one filter / one source) behavior"
```

---

## Notes for the implementer

- **Token identity:** the token passed to every `claim_*`/`release_*`/`*_owned_by_other` call is the instance's own data pointer (`FilterData*` for the filter, `SourceData*` for the source), reinterpreted as `const void*`. Never pass anything else; the pointer's identity is the whole mechanism.
- **Input vs output guards are independent.** The normal one-filter + one-source setup has the filter owning input and the source owning output, with no interaction — don't conflate them.
- **`release_input` may call `stop()`**, which joins the WebSocket thread. That's fine in `filter_destroy` / on key-clear. `release_output` does not stop anything (the session can keep running for a re-added source).
- Builds and `ctest` run on the **Windows box over SSH** with the `build_x64` tree (mac has no libobs).
```
