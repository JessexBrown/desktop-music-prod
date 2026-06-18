<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0043: Timeline Clip Hit-Testing

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0042 added deterministic imported clip selection state, but users could not
choose between imported clips from the native timeline lane. The next useful
selection step is mouse hit-testing for existing imported audio clip rectangles
without introducing drag movement, resizing, relinking, waveform editing, or undo
scope.

Hit-testing must stay outside the audio callback and should not depend on JUCE so
it can be unit tested with the same lane model used by the native app.

## Decision

Extend `TimelineClipLaneItem` with a `selected` flag derived from
`ProjectModel::getSelectedClipId()`. Add `hitTestTimelineClipLane` to the core
lane model. The function accepts lane-local pixel coordinates, ignores offscreen
items, and returns the imported clip id plus its track/clip/source indices for
the first matching visible rectangle.

The JUCE workspace panel translates mouse clicks into lane-local coordinates and
delegates selection to `MainComponent`. `MainComponent` calls
`AppSession::selectImportedAudioClip`, then refreshes the timeline lane and
inspector. The timeline renderer draws a subtle accent outline around the
selected clip.

## Consequences

- Timeline selection is now usable from the native JUCE shell.
- The hit-test behavior is covered by core unit tests and is independent of UI
  toolkit types.
- No drag/drop, resize, relinking, waveform editing, or plugin work is added.
- No audio callback behavior changes.
- No new dependency is introduced.

## Follow-Ups

- Design imported clip media relink chooser flow.
- Add clip movement and resizing only after explicit edit previews and undo are
  defined.
