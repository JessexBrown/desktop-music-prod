<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0055: Compact Timeline Pan Controls

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0054 added visible reset and zoom controls beside the persistent timeline
viewport indicator. Focused keyboard commands could already pan the timeline,
but mouse users still lacked a compact visible path for horizontal navigation.

The control strip should continue to live in the workspace subtitle row, near
the viewport indicator, and avoid adding global shortcuts or additional top-bar
density.

## Decision

Extend the opt-in workspace viewport control strip to five fixed-size buttons:

- `<`: pan the timeline view left by four beats;
- `0`: reset the timeline view start to beat zero;
- `-`: zoom the timeline view out;
- `+`: zoom the timeline view in;
- `>`: pan the timeline view right by four beats.

The pan buttons call the same `panTimelineViewport(-4.0)` and
`panTimelineViewport(4.0)` paths used by the focused workspace keyboard
commands. Each action refreshes the timeline lane and viewport indicator.

This ADR does not add clip editing, drag/drop behavior, global shortcuts, or
command-palette entries.

## Consequences

- Timeline viewport pan, reset, and zoom are all discoverable from the central
  workspace.
- Keyboard and mouse viewport behavior share the same session methods.
- Browser, inspector, device, and mixer panels still hide viewport controls
  because the callbacks are opt-in.
- No audio callback behavior changes.
- No dependency is added.

## Follow-Ups

- Add imported media package inventory model.
- Keep global shortcuts and command palette work behind the command registry
  focus policy.
