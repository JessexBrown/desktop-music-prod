<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0045: Workspace Keyboard Focus Feedback

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0044 added left/right arrow selection for imported timeline clips while the
JUCE workspace panel owns keyboard focus. The app needed a visible focus cue so
users can tell which surface will receive timeline selection shortcuts, without
confusing that state with the selected clip outline added in ADR-0043.

Focus routing is UI state only. It must not become project state, audio callback
state, or part of the persisted manifest.

## Decision

Draw a subtle cool-blue outline around the workspace panel when it owns keyboard
focus and has timeline keyboard-selection callbacks installed. Keep the existing
green selected-clip outline on individual clip rectangles.

Only panels with timeline keyboard callbacks consume left/right arrow keys. Other
placeholder panels may receive mouse focus through the shared component class,
but they do not display the workspace focus outline and do not claim timeline
selection shortcuts.

## Consequences

- Keyboard shortcut ownership is visible before users press left/right arrows.
- Focus feedback remains visually distinct from selected timeline clips.
- No core model, project manifest, audio callback, or dependency behavior
  changes.
- No new dependency is introduced.

## Follow-Ups

- Add a command routing layer before global shortcuts, command palette, or text
  editing surfaces expand.
- Add undo boundaries before editable inspector or timeline clip operations.
- Add focused workspace keyboard or toolbar controls to change viewport start
  and zoom.
