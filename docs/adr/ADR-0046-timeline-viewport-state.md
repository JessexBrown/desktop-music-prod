<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0046: Timeline Viewport State

## Status

Accepted for the v0.1 prototype.

## Context

Timeline clip rendering previously used default `TimelineClipLaneOptions` at the
JUCE app boundary. That kept the prototype simple, but it left no session-level
place for future horizontal scroll and zoom controls. Milestone 3 needs timeline
zoom/scroll to grow incrementally before visible scrollbars, minimaps, or
editable clip operations are added.

Viewport state is UI/session state. It should not affect audio playback,
project identity, or saved musical data.

## Decision

Add `TimelineViewportState` to `AppSession` with:
- `viewStartBeats`, clamped to finite non-negative values;
- `beatsPerPixel`, clamped to a prototype zoom range from `1/64` to `4` beats
  per pixel.

The default remains `0.125` beats per pixel so existing timeline placement keeps
the same scale until controls are added. The JUCE app now builds
`TimelineClipLaneOptions` from `AppSession::getTimelineViewport()` and the
workspace clip area's current width instead of relying on hard-coded lane
defaults at the app boundary.

The state is not persisted in `manifest.json` and is not read by the audio
callback.

## Consequences

- Future scroll/zoom controls have a validated session state boundary.
- Timeline lane layout can be generated from app viewport state.
- Project save/load remains focused on song data rather than transient UI view
  state.
- No visible scrollbars, minimaps, clip movement, or waveform editing are added.
- No new dependency is introduced.

## Follow-Ups

- Add imported clip media replacement undo tests.
