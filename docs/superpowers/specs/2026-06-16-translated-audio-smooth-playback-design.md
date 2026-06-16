# Translated Audio Smooth Playback Design

## Problem

The current output path can avoid permanently stuck translated tails, but it can
still sound unnatural. When translated audio underruns, OBS receives silence.
If more tail audio arrives later and is flushed after input has been idle, the
listener hears a gap followed by a late ending. That sounds like broken,
stuttering speech even if the final audio bytes are eventually emitted.

For live use, a small additional translation delay is acceptable. The priority
is smooth, continuous translated speech with no abrupt cut at sentence endings.

## Goals

- Prefer smooth playback over minimum latency.
- Use a larger startup buffer before translated audio begins.
- Once playback starts, keep draining translated audio until it is truly near
  empty instead of stopping when it falls below the startup threshold.
- Add a short end-of-utterance grace window so late Gemini tail audio can join
  the same playback run.
- Fade out only after the grace window expires without new audio.

## Non-Goals

- Do not add source-language selection.
- Do not add captions.
- Do not implement time-stretching or synthetic comfort noise in this pass.
- Do not add new OBS UI controls yet; use conservative constants first.

## Playback Policy

The output jitter logic should become a small state machine:

- `Priming`: playback has not started. Wait until output buffer reaches a
  1200 ms startup threshold of 24 kHz mono PCM.
- `Playing`: emit fixed 20 ms packets continuously. The startup threshold no
  longer matters once playback has begun.
- `Grace`: if the buffer has less than one packet while playing, do not
  immediately end the utterance. Wait up to 500 ms for more translated audio.
- `Stopped`: after the grace window expires with no new audio, fade out and
  return to priming.

If new output audio arrives during `Grace`, playback returns to `Playing`.
This prevents the common case where Gemini sends the last syllable or tail
slightly later than the rest of the translated speech.

The existing input-idle tail flush can remain as a secondary safety mechanism,
but it should not be the primary way sentence endings become audible. The main
fix is to avoid entering a hard audible gap before the tail arrives.

## Constants

Initial constants:

- Startup buffer: 1200 ms.
- Packet size: 20 ms.
- Grace window: 500 ms.
- Fade duration: 5-10 ms.

These values favor recording and stream smoothness. If user testing shows the
delay is too high, the startup buffer can be reduced to 1000 ms before adding
any UI setting.

## Testing

Add unit tests for the pure playback policy:

- It does not start before the startup threshold.
- It starts once the startup threshold is reached.
- After starting, it continues playback below the startup threshold.
- A short underrun enters grace instead of stopping immediately.
- New audio during grace resumes normal playback.
- Grace timeout stops playback and allows fade-out.
- Remaining tail audio can still drain, but it should not require a later spoken
  input segment to become audible.

Manual verification should use OBS recording, not monitoring, because monitoring
can introduce feedback and misleading repeats.
