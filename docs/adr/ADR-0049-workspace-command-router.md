<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0049: Workspace Command Router

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0048 defined the routing boundary for focused workspace commands but the
JUCE `WorkspacePanel::keyPressed` implementation still encoded the shortcut
table directly. The next small step is to preserve the existing focused
workspace behavior while moving the routing decision into a testable helper that
does not depend on JUCE types.

This is not the app-wide command palette, shortcut editor, or global shortcut
registry. It is a local routing table for existing workspace shortcuts.

## Decision

Add `WorkspaceCommandRouter` to `projectname_core`. It accepts:
- a small key enum;
- a command-modifier flag;
- availability flags for the callbacks installed on the current workspace.

It returns an optional `WorkspaceCommand`. The JUCE panel translates
`juce::KeyPress` into the router input, then executes the existing callback for
the returned command. Current behavior is preserved:
- left/right select previous/next imported clips when selection callbacks exist;
- command/ctrl-left/right pan the timeline viewport when viewport callbacks
  exist;
- command/ctrl-up/down zoom the timeline viewport when viewport callbacks exist;
- command/ctrl-left does not fall back to plain left selection when pan is
  unavailable.

## Consequences

- Existing focused workspace shortcuts are covered by core unit tests.
- JUCE key handling no longer owns the shortcut mapping table.
- The router remains local to workspace commands and does not introduce
  app-global shortcuts, command palette UI, clip movement, waveform editing, or
  plugin work.
- No audio callback behavior changes.
- No new dependency is introduced.

## Follow-Ups

- Design package-local media quarantine and restore commands.
