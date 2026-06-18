<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0053: Timeline Viewport Indicator

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0046 added session-owned timeline viewport state and ADR-0047 added focused
workspace keyboard pan/zoom commands. Users could pan or zoom the timeline, but
the app only reported the change transiently in the status line. The current
top bar is already dense, so a persistent indicator needed to be compact and fit
the existing workspace without creating another control surface.

## Decision

Add a small core formatter,
`formatTimelineViewportIndicator(TimelineViewportState)`, that renders the
current view start and beat scale as human-readable text:

```text
Timeline view: start 0.00 beats | 0.1250 beats/px
```

The formatter reuses the same normalization policy as the session viewport
state so invalid or non-finite values produce safe display text. The JUCE
workspace subtitle now shows the formatted viewport indicator whenever the
timeline lane refreshes, including after focused keyboard pan/zoom commands.

This ADR does not add new shortcuts, drag/drop behavior, editing commands, or
timeline control buttons.

## Consequences

- Timeline pan/zoom state is persistently visible in the main workspace.
- Display text is covered by plain C++ tests.
- The top bar avoids more density while the prototype still lacks full viewport
  controls.
- No audio callback behavior changes.
- No dependency is added.

## Follow-Ups

- Design package-local media quarantine and restore commands.
- Keep global shortcuts and command palette work behind the command registry
  focus policy.
