<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0056: Timeline Fit-To-Clips Viewport Helper

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0053 through ADR-0055 made viewport state visible and added compact pan,
reset, and zoom controls. A natural next control is "fit imported clips", but
that behavior should be proven in the plain C++ model before adding another
button to the workspace UI.

The helper needs to match the existing timeline lane rules for imported audio
clips and avoid changing clip editing, drag/drop, or shortcut behavior.

## Decision

Add `fitTimelineViewportToImportedAudioClips` to the app-session core model.
The helper:

- scans project tracks for usable imported audio clips;
- ignores generated clips, non-audio clips, non-finite positions, and clips with
  non-positive lengths;
- returns no value for empty projects or invalid viewport widths;
- applies a small beat padding around the imported clip span;
- returns a `TimelineViewportState` containing the proposed view start and
  beats-per-pixel scale.

The first tests cover:

- an empty/default project;
- invalid imported clips;
- a single imported clip;
- multiple spaced imported clips;
- a lane-builder proof that computed viewport values make imported clip
  rectangles visible inside the requested lane width.

This ADR does not add a visible fit button, clip editing, drag/drop behavior, or
global shortcuts.

## Consequences

- Fit-to-clips behavior is now testable without JUCE.
- A future UI control can call a small model helper rather than duplicating
  project geometry math in `MainComponent`.
- Imported clip visibility remains consistent with existing lane geometry.
- No audio callback behavior changes.
- No dependency is added.

## Follow-Ups

- Add imported clip media relink preparation model.
- Keep global shortcuts and command palette work behind the command registry
  focus policy.
