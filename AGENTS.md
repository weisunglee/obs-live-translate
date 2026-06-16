# AGENTS.md

## What this repo is

A native **Windows** OBS Studio plugin that does real-time speech-to-speech
translation via the Google Gemini Live API (`gemini-3.5-live-translate-preview`).
A microphone's audio is streamed to Gemini and the translated audio is emitted
as a separate OBS audio source that can live on its own track.

The code does not exist yet. Your job is to **implement it from the plan**.

## How to execute

1. Read the design spec for context:
   `docs/superpowers/specs/2026-06-16-obs-live-translate-design.md`
2. Open the implementation plan:
   `docs/superpowers/plans/2026-06-16-obs-live-translate-plugin.md`
3. Read the plan's **"Prerequisites & Environment Setup (Windows)"** section and
   set up the toolchain (Visual Studio 2022, CMake ≥ 3.24, Git, and libobs).
4. Create a feature branch: `git checkout -b feature/implementation`.
5. Work the plan's tasks **in order, top to bottom (Task 0 → Task 11)**. Each
   task is small and self-contained with exact files, full code, and commands.

## Rules

- **Test-driven.** For each task: write the failing test → run it and confirm it
  fails → write the minimal implementation → run it and confirm it passes →
  commit. The plan gives the exact commands and expected output. A task is done
  only when its command produces the stated expected result.
- **Commit after every task**, using the commit message in that task's final
  step. Do not squash tasks together.
- **Do not skip verification.** If a build or test command can't run because a
  prerequisite (e.g. libobs) isn't installed, stop and report exactly what is
  missing instead of marking the task complete.
- The **unit test target (`unit-tests`) does not need libobs** — Tasks 2–6 can be
  built and verified before the OBS toolchain is fully set up. Tasks 0, 1, 7, 8,
  9 require libobs.
- Do not invent features beyond the plan. The spec's non-goals (captions,
  multi-session, encrypted key storage, source-language selection) are out of
  scope for v1.

## Key technical facts (already verified against Google's docs)

- Model: `models/gemini-3.5-live-translate-preview`, WebSocket (TLS) endpoint.
- Input audio to Gemini: 16 kHz, 16-bit PCM, mono, little-endian, 100 ms chunks
  (3200 bytes/chunk).
- Output audio from Gemini: 24 kHz, 16-bit PCM, mono.
- `translationConfig` has only `targetLanguageCode` (BCP-47) and
  `echoTargetLanguage` (hardcode `true` for v1). **No source-language param** —
  the model auto-detects the input language.
