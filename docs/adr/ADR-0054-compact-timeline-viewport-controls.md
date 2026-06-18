<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0054: Compact Timeline Viewport Controls

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0053 made timeline viewport state persistently visible in the workspace
subtitle. The next UX gap was discoverability: users could zoom and pan the
timeline with focused keyboard commands, but there was no visible mouse-driven
control for common viewport actions.

The current top bar is already dense, so viewport controls need to stay near the
workspace viewport indicator rather than competing with transport, project, and
audio-device controls.

## Decision

Add opt-in child buttons to `WorkspacePanel` for panels that expose timeline
viewport controls. The central workspace enables three compact controls in the
subtitle row:

- `0`: reset the timeline view start to beat zero;
- `-`: zoom the timeline view out;
- `+`: zoom the timeline view in.

The buttons route through existing `AppSession` viewport methods:

- reset uses `setTimelineViewStartBeats(0.0)`;
- zoom out uses the existing `zoomTimelineViewport(2.0)` path;
- zoom in uses the existing `zoomTimelineViewport(0.5)` path.

Each action refreshes the timeline lane and the viewport indicator. The focused
keyboard pan/zoom commands remain unchanged. Browser, inspector, device, and
mixer panels do not show these controls because their callbacks are not set.

This ADR does not add clip editing, drag/drop behavior, global shortcuts, or
timeline pan buttons.

## Consequences

- The timeline viewport has a small visible mouse path for reset and zoom.
- Workspace controls stay close to the persistent viewport indicator.
- Other panels can continue reusing `WorkspacePanel` without inheriting timeline
  controls.
- No audio callback behavior changes.
- No dependency is added.

## Follow-Ups

- Design imported clip media relink chooser flow.
- Keep global shortcuts and command palette work behind the command registry
  focus policy.
