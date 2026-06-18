<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0047: Timeline Viewport Keyboard Controls

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0046 added validated app/session timeline viewport state, but no user action
changed that state. The next small Milestone 3 step is to let users pan and zoom
the timeline from the focused workspace without adding visible scrollbars,
minimaps, clip movement, or waveform editing.

Plain left/right arrow keys already select previous/next imported clips in the
focused workspace. Viewport shortcuts must not steal that behavior.

## Decision

Add two `AppSession` viewport commands:
- `nudgeTimelineViewStartBeats`, which pans by a beat delta and reuses
  non-negative view-start clamping;
- `scaleTimelineBeatsPerPixel`, which zooms by a multiplier and reuses the
  ADR-0046 zoom range.

The JUCE workspace handles command/ctrl-modified arrow keys while focused:
- command/ctrl-left pans left by four beats;
- command/ctrl-right pans right by four beats;
- command/ctrl-up zooms in by halving beats per pixel;
- command/ctrl-down zooms out by doubling beats per pixel.

`MainComponent` refreshes the timeline lane after each viewport command and
updates the status line with the new view start or zoom scale.

## Consequences

- Users can pan and zoom the prototype timeline from the keyboard.
- Plain left/right clip selection remains unchanged.
- The viewport command math is covered by core unit tests.
- No visible scrollbars, minimaps, clip movement, waveform editing, persistence,
  or audio callback behavior is introduced.
- No new dependency is introduced.

## Follow-Ups

- Add visible viewport controls or a compact zoom indicator after the command
  routing layer is clearer.
- Add compact visible timeline viewport controls.
- Add undo boundaries before editable inspector or timeline clip operations.
