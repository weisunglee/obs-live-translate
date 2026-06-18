# Single-session guard: first-wins ownership + warning

## Problem

The whole plugin shares one global `TranslationSession` singleton
(`TranslationSession::instance()`), which owns a single WebSocket, a single
input ring buffer, a single output ring buffer, and a single
`{api_key, target_lang, echo_target}` config. Both the filter and the
translated-audio source talk to that one instance.

Nothing prevents a user from adding more than one of either source, which
silently corrupts behavior:

- **Two filters** — each calls `configure(...)` on the same singleton
  (`filter.cpp`), and `filter_update` fires on load and on every settings
  change, so whichever fires last wins. Both filters' languages are saved
  correctly per-filter in the scene-collection file, but only one is ever active
  at runtime (looks like "the language isn't saved separately"). Worse, both
  filters' `filter_audio` push resampled PCM into the *same* `input_` ring
  buffer, interleaving two audio streams into one → Gemini receives garbage →
  garbled/broken output.

- **Two translated-audio sources** — `wait_and_read_output` is a *consuming*
  read, so two source instances each drain chunks from the one shared `output_`
  buffer and steal from each other → the translated voice is split between them
  and breaks up.

Supporting multiple *simultaneous* translations (e.g. one mic → English **and**
Japanese at once) is an explicit v1 non-goal. The goal here is only to stop the
silent breakage: keep the single-session design, but make the plugin behave
predictably when a duplicate is added.

## Goals

- Exactly one filter is the active **input owner**; extra filters are disabled,
  pass audio through untouched, and surface a visible warning.
- Exactly one source is the active **output owner**; extra sources stay silent
  and surface a visible warning.
- **First-wins**: the first filter/source to claim a resource keeps it; a
  duplicate added later does not hijack a working translation.
- A duplicate automatically takes over when the active owner is removed — no
  need to re-open properties.

## Non-goals

- Multiple simultaneous translation sessions / multiple target languages at
  once (unchanged v1 non-goal).
- Any change to the translation pipeline, protocol, or audio pacing.

## Design

### Ownership in `TranslationSession`

The session has two independent resources: **input** (filters push PCM in) and
**output** (sources drain PCM out). Each gets its own owner token. The token is
just the caller's pointer (`FilterData*` / `SourceData*`) treated as an opaque
`const void*`.

```cpp
bool claim_input(const void* token);    // unowned -> take it, return true;
                                         // already ours -> true; owned by other -> false
void release_input(const void* token);  // only the owner may release; then unowned
bool claim_output(const void* token);
void release_output(const void* token);
```

Implementation: a small mutex per resource (or one mutex guarding both owner
pointers). `claim_*` is "take it only if unowned" under the lock, so the **first
successful claimer wins**; later callers get `false` until the owner releases.
`release_*` clears the owner only if the caller currently holds it (a non-owner
release is a no-op).

Input and output ownership are fully independent: the normal case (one filter +
one source) has the filter owning input and the source owning output, with no
interaction.

### Filter behavior

`FilterData` gains a local `bool active`.

- `filter_update` and `filter_audio` first call `claim_input(d)`:
  - Transition `false -> true` (just became owner): set `active = true`, apply
    this filter's `configure(api_key, target_lang, echo_target)`, refresh status
    text.
  - Already owner (`true`): behave as today — push audio; `filter_update`
    re-applies `configure`.
  - `false` (another filter owns input): set `active = false`, do **not**
    `configure`, do **not** push audio; return `audio` unchanged (clean
    pass-through), refresh status text.
- `filter_destroy` calls `release_input(d)`.

Warning surface: reuse the existing `status` info text field in the filter
properties. When not the owner, show:

> ⚠ Another Gemini Live Translate filter is already active — this one is
> disabled.

### Source behavior (symmetric)

`SourceData` gains a local `bool active`. `push_loop` calls `claim_output(d)` at
the top of each iteration:

- Owner: read the output buffer and call `obs_source_output_audio` as today.
- Not owner: do **not** read the buffer (avoid stealing chunks) — sleep briefly
  and loop, staying silent.

The source currently has no properties panel, so there is nowhere to show a
warning. Add a minimal `get_properties` to the source with a single `status`
info field. When not the owner, show:

> ⚠ Another Gemini Translated Audio source is already active — this one is
> muted.

`source_destroy` calls `release_output(d)`.

### Edge cases

- **Takeover**: when the active owner is removed, `release_*` makes the resource
  unowned. The remaining disabled filter/source claims it on its next
  `filter_audio` / `push_loop` iteration and takes over automatically (the
  filter re-applies its `configure` on the `false -> true` transition). No
  properties re-open required.
- **No active filter**: when `release_input` leaves input unowned, call `stop()`
  to tear down the WebSocket — with no active filter there is nothing to
  translate, so the plugin should not stay connected. The next filter to claim
  input starts it again via `configure`.
- **Independence**: input and output owners are separate, so the normal
  one-filter + one-source setup is unaffected.

## Testing

- **Unit (Catch2, no libobs)**: ownership logic is pure (mutex + pointers). Add
  a test group: first `claim_input` returns `true`; a second token's
  `claim_input` returns `false`; after the owner `release_input`s, the second
  token can claim; a non-owner `release_input` is a no-op. Same for the output
  pair.
- **Manual (Windows)**: add two filters to one mic → the second shows the
  warning and audio is not garbled; remove the first → the second takes over and
  translation resumes. Add two translated-audio sources → the second is silent
  and shows the warning; remove the first → the second starts playing.

## Out of scope / follow-ups

- README note documenting the single-filter / single-source limitation can be
  refreshed alongside this change, but the guard is the substantive fix.
