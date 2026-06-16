# Translated Audio Push-Output Design

## Problem

The translated-audio output path runs a fixed 20 ms wall-clock emit loop
(`source.cpp::emit_loop`) that pulls from a ring buffer and injects silence on
underrun. Gemini delivers translated audio in bursts, not at a strict real-time
cadence, so a fixed-clock consumer underruns constantly. To mask the resulting
choppiness, the current `OutputJitterBuffer` state machine added a 1200 ms
startup buffer plus a 500 ms grace window.

Two symptoms follow from that architecture:

- **Tail truncation (primary):** the client never parses Gemini's turn-boundary
  signals. It guesses sentence endings from `input_idle_ms()` plus the jitter
  grace window. When the buffer underruns before the last syllable arrives and
  the grace window expires, the state machine fades out and cuts the tail.
- **Latency (secondary):** the deliberate 1200 ms startup buffer is a hard
  latency floor before translated speech begins.

This design eliminates the local "decide to stop / fade out" logic entirely, so
no received audio is ever discarded by the client.

## Goals

- Never let the client truncate a sentence tail: every received PCM byte is
  handed to OBS and played.
- Stop running a self-clocked emit loop; let the OBS audio mixer be the clock.
- Parse Gemini turn-boundary signals (`turnComplete`, `generationComplete`,
  `interrupted`) and use `interrupted` to drop stale audio.
- Reduce startup latency as a free side effect (no 1200 ms priming buffer).

## Non-Goals

- Do not change the input-side voice gate (`VoiceGate`) or chunking. Input-side
  clipping is a separate concern, out of scope here.
- No captions, no source-language selection, no new OBS UI controls.
- No time-stretching or synthetic comfort noise.

## Architecture

The output source becomes an **event-driven asynchronous PCM source**. The
network thread writes decoded audio into the session's output buffer and
notifies a condition variable. The source's own thread wakes, drains all
available audio, and pushes it to `obs_source_output_audio` with **contiguous,
duration-spaced timestamps**. OBS buffers and schedules playback at the correct
rate. Reference pattern: `norihiro/obs-asynchronous-audio-source`.

`obs_source_output_audio` is called only from the source's own thread (not the
network thread), so source lifetime stays simple: the thread is joined on
destroy and there is no cross-thread call into a source that is being torn down.

### Data flow

```
mic → filter (resample 16k, gate, chunk) → session.input_
    → network thread → Gemini Live (WebSocket)
    → audio chunks → session.output_ + notify CV
    → source push thread → obs_source_output_audio → OBS mixer (clock + buffer)
    → translated-audio track
```

## Components

### live-protocol

Extend `ServerMessage` to carry flags instead of a single mutually-exclusive
`Kind`, because one server message can contain both audio and `turnComplete`:

- `std::vector<uint8_t> audio;` (may be empty)
- `bool turn_complete = false;`
- `bool generation_complete = false;`
- `bool interrupted = false;`
- existing error fields retained.

`parse_server_message` additionally reads `serverContent.turnComplete`,
`serverContent.generationComplete`, and `serverContent.interrupted`. Audio
extraction from `serverContent.modelTurn.parts[].inlineData` is unchanged.

### TranslationSession

- On an audio message: write PCM to `output_` and notify a new output
  condition variable.
- On `interrupted`: clear `output_` (drop the stale, interrupted translation)
  and raise an interrupt flag, then notify.
- New API for the source thread:
  - block until output bytes are available, stop is requested, or a short
    timeout elapses, then read all currently available bytes (caller passes a
    per-call byte cap);
  - `bool take_interrupted();` — returns and clears the interrupt flag.
- Output decisions no longer use `input_idle_ms()`. (`input_idle_ms` may remain
  for any input-side use but is irrelevant to output.)

### source

Replace the fixed-clock `emit_loop` and `OutputJitterBuffer` with a push loop:

- State: `uint64_t next_ts = 0;`
- Loop while active:
  1. Wait on the session's output CV (with stop check and a short timeout).
  2. If the interrupt flag is set, reset `next_ts = 0`.
  3. Drain all available output bytes, capped per iteration (a few hundred ms
     worth) so a single `obs_source_output_audio` call is bounded; align to
     whole 16-bit mono frames.
  4. `ts = max(next_ts, os_gettime_ns());`
  5. `obs_source_output_audio(ctx, pcm, frames, 24 kHz, mono, S16, ts);`
  6. `next_ts = ts + frames * 1'000'000'000 / 24000;`
- The `max(next_ts, now)` clamp self-heals turn gaps: during a burst `next_ts`
  runs ahead and audio stays contiguous; after silence `next_ts` falls behind
  `now` and is clamped forward, starting the next utterance cleanly. No
  heuristic end-of-turn detection is needed.
- Guard: if `next_ts` leads `now` by more than ~2 s, log it (indicates the model
  is producing audio faster than real time for an unexpectedly long stretch and
  OBS may begin dropping samples).
- No silence injection, no jitter buffer, no `DrainPartial`/`Hold`/`Priming`.

### Removed

`OutputJitterBuffer` and its state machine, `output_jitter_start_bytes()`,
`output_jitter_grace_ms()`, the 1200 ms startup buffer, the 20 ms sleep clock,
and the silence `memset`. `PcmS16MonoSmoother` is removed for v1; a short fade is
re-introduced only if testing reveals audible clicks, in which case
`generation_complete` marks where a turn's final audio ends.

## Why this fixes truncation

The client no longer decides to stop or fade. Every received byte is pushed to
OBS, so the tail is played whenever it arrives. This is also robust to Gemini's
known premature-`turnComplete` bug (googleapis/python-genai#2117,
googleapis/js-genai#707): because audio is never gated on `turnComplete`, an
early `turnComplete` cannot cut the audio.

## turnComplete / interrupted usage

- `turnComplete` / `generationComplete`: clean turn boundaries. The timestamp
  clamp already handles the gap, so these are informational for v1 (logging, and
  marking the final-audio point if a fade is later added).
- `interrupted`: clear pending output and reset `next_ts`, so translated audio
  from an interrupted turn is not played.

## Error handling

- Malformed/partial server JSON: unchanged (discarded message → no audio, no
  flags).
- Auth/server errors: unchanged session reconnect/backoff behavior.
- Output buffer overrun (producer faster than consumer drains): the bounded ring
  buffer keeps dropping oldest, as today; in practice the source thread drains
  promptly because it is event-driven.

## Testing

- Extract the pure timestamp logic into an `OutputTimestamper` unit that takes
  per-chunk frame counts and an injected clock and produces timestamps. It must
  build without libobs and replaces the `OutputJitterBuffer` unit tests:
  - contiguous timestamps across consecutive chunks within a burst;
  - clamp forward to `now` after a gap (next timestamp is not in the past);
  - reset on interrupt;
  - lead-guard threshold reached is observable.
- Manual verification uses OBS **recording**, not monitoring (monitoring can
  feed back and create misleading repeats). Confirm sentence tails are intact
  and startup latency is materially lower than the 1200 ms baseline.

## Risks

- OBS caps asynchronous-audio buffering; if the model sustains a
  faster-than-real-time burst long enough, OBS drops excess samples (source
  clock faster than OBS) and may pop. Expected to be fine because translated
  audio length tracks speech length and turns reset the lead, but watch for it
  in testing.
- Timestamp lead growth is bounded only by turn structure; the lead-guard log
  surfaces pathological cases.
