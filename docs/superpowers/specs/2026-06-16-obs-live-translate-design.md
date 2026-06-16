# OBS Live Translate Plugin — Design

**Date:** 2026-06-16
**Status:** Approved design, ready for implementation planning

## Summary

A native OBS Studio plugin (Windows) that performs real-time speech-to-speech
translation of an audio source using Google's Gemini Live API
(`gemini-3.5-live-translate-preview`). The user picks a target language and
enters their own Gemini API key. Incoming audio is streamed to Gemini,
translated audio is streamed back, and that translated audio is emitted as a
**separate OBS audio source** that can be assigned to its own track for
independent monitoring and output.

## Goals (v1 / MVP)

1. **Core translation pipeline** — capture audio → stream to Gemini → emit
   translated audio on a dedicated track.
2. **Configuration UI** — select target language, enter API key.
3. **Auto-reconnect** — exponential backoff on network/WebSocket drops.

## Non-goals (future versions)

- On-screen captions / transcripts (Gemini returns input/output text, but not
  surfaced in v1).
- Multiple simultaneous translation sessions (multi-mic / multi-language).
- Encrypted API key storage (Windows DPAPI).
- Source-language selection — **not possible**: the Gemini Live API
  auto-detects the input language and accepts no source-language parameter.
- macOS / Linux builds.
- Installer / packaging beyond a manual `.dll` drop (or zip).

## Platform & technology decisions

- **Platform:** Windows only. OBS plugin is a `.dll`; audio via OBS internals
  (no direct WASAPI needed for v1 — we use OBS sources/filters).
- **Language:** Native C++ OBS plugin. The Gemini Live SDK ships only for
  Python/JS, so we hand-roll the Live API protocol over a C++ WebSocket client.
  This keeps distribution to a single self-contained `.dll`.
- **Target OBS:** OBS 30.x (libobs API v30).

## Gemini Live API constraints (verified against docs)

Reference: https://ai.google.dev/gemini-api/docs/live-api/live-translate

- **Model:** `models/gemini-3.5-live-translate-preview`. Direct
  speech-to-speech, 70+ languages.
- **Connection:** WebSocket, `wss://generativelanguage.googleapis.com/ws/...BidiGenerateContent?key=<API_KEY>` (TLS).
- **Input audio:** 16 kHz, 16-bit PCM, mono, little-endian, streamed in ~100 ms
  chunks (1600 samples = 3200 bytes per chunk) as base64
  `realtimeInput` blobs with `mimeType: "audio/pcm;rate=16000"`.
- **Output audio:** 24 kHz, 16-bit PCM, mono, returned as base64 blobs in
  `serverContent` messages.
- **translationConfig fields (only two exist):**
  - `targetLanguageCode` — BCP-47 (e.g. `"zh-TW"`, `"en"`, `"ja"`).
  - `echoTargetLanguage` — boolean; when input is already the target language,
    `true` = still output it, `false` = stay silent. **v1 hardcodes `true`.**
- **responseModalities:** `["AUDIO"]`.
- **Known model limitations (documented, not fixable by us):** auto language
  detection weaker on heavy accents / similar languages; imperfect background
  noise filtering; voice cloning may be unstable. Inherent multi-second latency.

## Architecture

A single `obs-live-translate.dll` registers two OBS types plus one background
session object.

### Components

1. **"Gemini Live Translate" audio filter** (`OBS_SOURCE_TYPE_FILTER`,
   audio filter)
   - Attached to any audio source (typically the mic).
   - `filter_audio` callback copies the audio into the session input buffer and
     **returns the original audio unchanged** (downstream audio is untouched).
   - Owns the configuration: target language, API key, connection status.

2. **"Gemini Translated Audio" custom input source** (`OBS_SOURCE_TYPE_INPUT`,
   audio)
   - Added to a scene. Produces no input of its own; it only emits translated
     audio via `obs_source_output_audio`.
   - Assigned to its own track in **Advanced Audio Properties** for independent
     monitoring / output.

3. **`TranslationSession` (background core)**
   - The broker between filter ↔ network ↔ source.
   - Holds: bounded input ring buffer, a dedicated **network thread** that owns
     the WebSocket, bounded output ring buffer.
   - **v1: a single global session.** Only one translation pipeline at a time
     (one mic → one target language). The filter (writer) and source (reader)
     auto-bind to this global session.

### Data flow

```
OBS source audio
  → filter (tap, copy)
  → input ring buffer
  → network thread → Gemini Live (wss) → network thread (recv translated audio)
  → output ring buffer
  → custom source (obs_source_output_audio)
  → dedicated audio track
```

**Key rationale:** `filter_audio` runs on the OBS audio thread and must never
block. It only copies + enqueues. All network I/O lives on the dedicated thread;
the two sides are decoupled by ring buffers.

## Audio data flow & format conversion

### Input (filter → Gemini) — we convert

- OBS delivers `obs_audio_data`: float planar, OBS mix rate (e.g. 48 kHz),
  multi-channel.
- Gemini requires 16 kHz / 16-bit PCM / mono / little-endian in 100 ms chunks.
- Conversion: **downmix to mono → resample 48k→16k → float→16-bit PCM → chunk
  into 100 ms blocks.**
- Resampling uses the **OBS built-in `audio_resampler`** (libobs wraps ffmpeg
  swresample) — no extra dependency, no custom resampler.

### Output (Gemini → source) — OBS handles it

- Gemini returns 24 kHz / 16-bit PCM / mono.
- The custom source calls `obs_source_output_audio` declaring
  `samples_per_sec=24000, format=AUDIO_FORMAT_16BIT, speakers=SPEAKERS_MONO`.
  **OBS automatically resamples to the mix rate and mixes** — no output-side
  resampling on our part.

### Timestamps / sync

- Translated audio is timestamped with `os_gettime_ns()` at emit time.
- Speech-to-speech has inherent multi-second latency, so the translated track is
  **not** time-aligned with the original — which is precisely why it lives on a
  separate track (no lip-sync expectation).

### Buffer overflow

- Bounded ring buffers. If the input buffer fills (network slower than input),
  **drop oldest data** — prioritize liveness, prevent unbounded latency growth.

## Networking layer (hand-rolled C++)

### Connection lifecycle

1. Open WebSocket (TLS) to the BidiGenerateContent endpoint with `?key=<API_KEY>`.
2. Send the **setup message**: model
   `models/gemini-3.5-live-translate-preview`, `responseModalities: ["AUDIO"]`,
   `translationConfig: { targetLanguageCode, echoTargetLanguage: true }`.
3. Continuously send `realtimeInput` audio blobs from the input buffer.
4. Receive `serverContent` messages, base64-decode audio, push to output buffer.

### Dependencies (vendored, compiled into the .dll)

- **IXWebSocket** — lightweight cross-platform WebSocket client with built-in
  TLS. TLS backend: **mbedTLS** (bundled, avoids requiring OpenSSL on the host).
- **nlohmann/json** — build/parse Live API JSON messages.
- **base64 helper** — small (~30 lines), written in-tree.
- Managed via git submodule or CMake FetchContent; statically linked.

### Reconnection

- Network thread detects socket close/error → **exponential backoff** reconnect
  (1s, 2s, 4s … capped at 30s), re-sending the setup message on reconnect.
- **API key invalid / setup rejected → show error status, stop streaming, do
  NOT reconnect** (reconnecting cannot help).
- Connection status (connecting / connected / reconnecting / API key error) is
  shown as read-only text in the filter properties and written to the OBS log.

## Configuration UI & API key storage

### Filter properties panel

- **Target language** — dropdown of common BCP-47 codes (en, zh-TW, ja, ko, es,
  fr, de, …; extensible). Maps to `targetLanguageCode`.
- **API key** — text field with `OBS_TEXT_PASSWORD` type (masked input).
- **Connection status** — read-only text.

### API key storage

- Stored per OBS convention in the **scene collection JSON**
  (`%APPDATA%\obs-studio\basic\scenes\*.json`) — i.e. **plaintext**, like nearly
  all OBS plugins.
- A warning line next to the field reminds the user the key is saved in the
  scene file and that file should not be shared.
- Encrypted storage (Windows DPAPI) is deferred to a future version.

## Error handling

| Situation | Handling |
|---|---|
| API key invalid / setup rejected | Show error status, stop streaming, **no reconnect** |
| Network / WebSocket drop | Exponential backoff reconnect (1→30s), resend setup |
| Input buffer full (slow network) | Drop oldest data, prioritize liveness |
| Unexpected / malformed message | Log, skip the message, do not tear down the session |
| Filter present but source not added (or vice versa) | Each operates normally; with no peer, data is discarded; status text hints |

## Testing strategy

OBS integration is hard to automate, so pure logic is factored into
independently testable units.

- **Unit-testable (no OBS / no network):**
  - Audio conversion: downmix, resample, float→PCM, 100 ms chunking.
  - base64 encode/decode.
  - Live API message construction & parsing (against fake JSON).
  - Ring buffer full/empty/drop-oldest behavior.
  - Reconnect backoff calculation.
- **Integration (semi-automated):** a standalone test harness reads a WAV file,
  runs the full pipeline against a real Gemini session, writes translated audio
  to a WAV. Run manually with an API key; verified by ear.
- **OBS layer:** manual acceptance (attach filter, add source, assign track,
  listen to translated audio).

## Build & distribution

- **Build:** CMake based on the official `obs-plugintemplate`, linking libobs.
  Produces `obs-live-translate.dll`.
- **Dependencies:** IXWebSocket / nlohmann/json / mbedTLS vendored and
  statically linked — no extra runtime install for the user.
- **Install:** drop the `.dll` into
  `C:\Program Files\obs-studio\obs-plugins\64bit\`. v1 ships as a manual drop /
  zip; an installer is a future improvement.
- **Target:** OBS 30.x (libobs API v30).
