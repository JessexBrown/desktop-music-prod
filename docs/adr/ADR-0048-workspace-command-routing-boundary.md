<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0048: Workspace Command Routing Boundary

## Status

Accepted for the v0.1 prototype.

## Context

The prototype now has several focused workspace commands:
- ADR-0044 added plain left/right arrows for previous/next imported clip
  selection.
- ADR-0047 added command/ctrl-left/right for timeline viewport pan and
  command/ctrl-up/down for timeline viewport zoom.

These were intentionally local to the focused JUCE workspace panel. GOAL.md also
calls for a future command palette, shortcut editor, piano roll, device editors,
and global app commands. Without an explicit boundary, shortcut handling could
grow into ad hoc component code that steals keys from text fields, future
editors, or platform conventions.

## Decision

Treat command routing as layered:

1. Focused editor/surface commands run first. The workspace owns commands that
   directly manipulate the visible workspace surface or its selected object.
2. App-global commands run only when no focused surface handles the shortcut.
   Transport, save/open/import, audio setup, command palette, and future global
   search belong here.
3. Text entry and modal/system components keep their native key behavior before
   either project-level command layer expands.

Current local workspace commands:
- left arrow: select previous imported audio clip;
- right arrow: select next imported audio clip;
- command/ctrl-left: pan the timeline viewport left;
- command/ctrl-right: pan the timeline viewport right;
- command/ctrl-up: zoom the timeline viewport in;
- command/ctrl-down: zoom the timeline viewport out.

Current app-global commands remain explicit UI actions rather than keyboard
bindings:
- Play and Stop;
- Save and Open;
- Import and Cancel Import;
- Cancel timeline preparation;
- Audio/MIDI settings;
- mixer control edits from the static mix strip.

Until a command registry exists, new shortcuts should be added through small
surface-specific callbacks or a narrow routing object, documented in an ADR when
they affect user-visible workflow. Do not add global shortcuts by scattering
`keyPressed` overrides across unrelated components.

## Consequences

- Existing workspace shortcuts remain local and predictable.
- Future command palette/shortcut editor work has a boundary to preserve focus
  ownership.
- The current slice adds no new editing commands, plugin work, command palette
  UI, clip movement, or waveform editing.
- No audio callback behavior changes.
- No new dependency is introduced.

## Follow-Ups

- Add package media quarantine preflight plan model.
