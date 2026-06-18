<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0025: Loop Region Timeline Rendering

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0023 added persisted loop-region state, and ADR-0024 made session transport
advancement wrap through enabled loop regions. The timeline lane still needed a
visual range affordance so the loop state is inspectable in the workspace.

The loop affordance must share the same beat-to-pixel scaling as imported clip
rectangles and must not decode audio, read project files, or touch the realtime
audio callback from paint code.

## Decision

Extend `TimelineClipLaneLayout` with an optional `TimelineLoopRange`.

The layout builder:

- reads the already-loaded project loop region;
- omits the range when the loop is disabled;
- scales loop start and length using the lane view start and beats-per-pixel;
- preserves raw `x` and `width` values so renderers can clip against their
  local paint bounds;
- marks offscreen loop ranges as not visible.

The JUCE workspace and Win32 fallback draw the loop range behind clip rectangles
using the shared layout values. Disabled-loop projects keep the previous visual
state.

## Consequences

- Loop state is now visible in both native workspace paths.
- Loop range scaling and clipping are covered in the plain C++ test suite.
- Paint code still performs only drawing and rectangle clipping; it does not
  inspect manifests, decode audio, or touch audio state.
- No new dependency is introduced.

## Follow-Ups

- Add user-facing loop editing controls.
- Connect loop-aware session position to timeline audio scheduling.
- Add screenshot or UI smoke coverage once the JUCE app target is buildable
  locally or in CI.
