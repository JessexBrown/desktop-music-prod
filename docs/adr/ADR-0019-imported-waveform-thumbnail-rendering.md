<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0019: Imported Waveform Thumbnail Rendering

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0018 stores imported-audio waveform summaries under the package
`analysis/` folder, but the native workspace still showed only static placeholder
rows. Milestone 3 needs a basic waveform thumbnail path before richer timeline
editing, zooming, and clip lanes.

The renderer must not decode audio, mutate projects, or touch the realtime audio
callback. Missing analysis files should be visible and recoverable instead of
blocking project load.

## Decision

Add `WaveformThumbnail` to `projectname_core`.

The shared loader:

- finds the first imported `audio-file` clip in the project;
- resolves its package-relative `analysisPath`;
- rejects absolute or parent-directory analysis paths;
- loads `WaveformSummary` JSON when present;
- reports explicit states for no imported audio, ready, missing analysis, and
  invalid analysis;
- exposes sampled peak columns for lightweight drawing.

The JUCE workspace panel accepts a preloaded `WaveformThumbnail` and paints a
small non-interactive waveform clip when the summary is ready. Missing or
invalid analysis files render a compact recoverable status instead.

The Win32 fallback uses the same thumbnail model and a GDI drawing path so the
local MinGW build verifies the native rendering code path can compile.

## Consequences

- Imported audio now has a visible waveform thumbnail path without decoding audio
  in paint code.
- Project load still succeeds when analysis is missing or invalid.
- Unsafe analysis paths are treated as invalid instead of being resolved outside
  the package.
- The thumbnail is a first vertical slice only: no timeline zoom, scrolling,
  editing, or stale-analysis detection yet.

## Follow-Ups

- Background regeneration for missing or invalid summaries is recorded in
  ADR-0020; add stale-summary detection once source metadata is richer.
- Multi-clip lane scaling is recorded in ADR-0021; promote it into editable
  track lanes with user placement controls.
- Add UI smoke or screenshot coverage once the JUCE app target is buildable
  locally or in CI.
