<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0059: Compact Selected Clip Center Control

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0058 added a tested model helper for centering the selected imported audio
clip in the timeline viewport while preserving the current zoom scale. The
workspace already exposes compact viewport controls for pan, reset, fit, and
zoom, so selected-clip centering should use the same localized UI pattern rather
than adding a global shortcut or top-bar command.

## Decision

Add a compact `C` control beside the existing workspace viewport controls.

When clicked, the control:

- calls `centerTimelineViewportOnSelectedImportedAudioClip`;
- stores the returned `TimelineViewportState` on the app session;
- refreshes the timeline lane and viewport indicator through the existing
  workspace refresh path;
- shows a non-disruptive status-line message when no selected imported clip can
  be centered.

The control remains scoped to `WorkspacePanel` callbacks. It does not add a new
global shortcut, command-palette entry, plugin action, editable clip operation,
or audio-thread behavior.

## Consequences

- A selected imported clip can be brought to the center of the visible timeline
  without changing zoom.
- Empty, stale, or invalid selections are handled through the existing status
  line rather than a modal dialog.
- Viewport math remains in the tested core helper instead of being duplicated in
  the JUCE component.
- No dependency is added.

## Follow-Ups

- Add undo boundaries before editable inspector or timeline clip operations.
- Keep global shortcuts and command palette work behind the command registry
  focus policy.
