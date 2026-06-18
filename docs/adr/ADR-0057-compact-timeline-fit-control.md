<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0057: Compact Timeline Fit Control

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0056 added a tested plain C++ helper that computes a viewport capable of
showing usable imported audio clips. The workspace already has compact pan,
reset, and zoom controls beside the timeline viewport indicator, so the next
small UI step is to expose the helper there without introducing global
shortcuts, clip editing, drag/drop behavior, or command palette scope.

## Decision

Add a compact `Fit` control to the workspace viewport control row.

When clicked, the control:

- calls `fitTimelineViewportToImportedAudioClips`;
- stores the returned `TimelineViewportState` on the app session;
- refreshes the timeline lane and viewport indicator through the existing
  workspace refresh path;
- shows a non-disruptive status-line message if the project has no imported
  audio clips that can be fit.

The control remains a `WorkspacePanel` callback like the existing compact
viewport controls. It does not add a new global shortcut, app command registry
entry, plugin action, or audio-thread behavior.

## Consequences

- Imported clips can be brought into view with one click after import or manual
  viewport changes.
- The JUCE UI reuses the proven model helper instead of duplicating clip span
  calculations.
- Empty projects receive a status-line message rather than a modal dialog.
- No dependency is added.

## Follow-Ups

- Add imported clip media replacement undo tests.
- Keep global shortcuts and command palette work behind the command registry
  focus policy.
