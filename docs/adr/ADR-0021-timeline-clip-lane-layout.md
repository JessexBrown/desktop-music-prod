<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0021: Timeline Clip Lane Layout

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0019 introduced a first imported-waveform thumbnail path, but it only loaded
and rendered the first imported audio clip. Milestone 3 needs track lanes, clip
placement, and basic waveform thumbnails. Before adding interactive editing, the
app needs a shared model that can arrange multiple imported clips by beat
position without decoding audio in paint code.

## Decision

Add `TimelineClipLane` to `projectname_core`.

The layout builder:

- loads waveform thumbnail states for all imported audio clips in project order;
- keeps ready, missing-analysis, and invalid-analysis state per clip;
- filters out non-audio clips and unusable imported clips with invalid or
  non-positive beat lengths;
- sorts visible lane items by `startBeats` with stable source-order ties;
- converts `startBeats` and `lengthBeats` into integer pixel rectangles using a
  caller-provided beats-per-pixel value;
- assigns overlapping clips to deterministic stacked rows;
- reports content height and view end in beats for UI callers.

The JUCE workspace panel and Win32 fallback workspace now consume the shared
layout and draw multiple imported clip rectangles with their waveform thumbnails
or recoverable analysis status. Paint code still does not decode audio, touch
the project manifest, or call into realtime audio paths.

## Consequences

- Multiple imported audio clips can be represented visually without waiting for
  full timeline editing.
- Missing and invalid waveform summaries remain visible per clip instead of
  collapsing to the first imported clip only.
- The layout is testable in plain C++ and covered by the core test suite.
- Imported audio placement commands and deterministic import start-beat behavior
  are recorded in ADR-0022.
- No new dependency is introduced.

## Follow-Ups

- Promote placement commands into interactive drag/drop or inspector controls.
- Add timeline viewport zoom and horizontal scrolling state.
- Split the simple stacked lane into true per-track lanes once track editing
  exists.
- Add screenshot or UI smoke coverage once the JUCE app target is buildable
  locally or in CI.
