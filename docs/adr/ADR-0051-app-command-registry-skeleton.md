<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0051: App Command Registry Skeleton

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0050 defined the app command registry shape for future global shortcuts,
command palette work, and shortcut editing. The prototype already exposes a
small set of explicit top-bar buttons, but their command metadata and enabled
state were still implicit in JUCE component setup and background job callbacks.

The next slice needed to make command ids and availability testable without
adding global keyboard behavior, command palette UI, plugin scanning, or new
editing commands.

## Decision

Add a JUCE-free `AppCommandRegistry` to `projectname_core` with:

- stable dotted command ids for the current top-bar actions;
- labels, short descriptions, and scopes;
- enabled/disabled state plus disabled status text;
- duplicate-id rejection;
- a prototype factory that builds the current command set from plain
  availability flags.

The first registered commands are:

- `transport.play`;
- `transport.stop`;
- `project.save`;
- `project.open`;
- `audio.import`;
- `audio.import.cancel`;
- `timeline.preparation.cancel`;
- `audio.settings.show`.

`MainComponent` now derives the current top-bar button enabled state from the
registry on the UI/session thread. The inputs remain the existing app state:
whether the import chooser or import job is active, and whether the audio import
or timeline preparation job can still be cancelled. Button click execution stays
in the existing handlers for this slice.

This ADR does not add command handlers, shortcuts, a command palette, or a
shortcut editor.

## Consequences

- App-global command ids and metadata are now tested in the plain C++ suite.
- Current top-bar enablement is centralized without changing visible behavior.
- Import and timeline-preparation cancellation availability remains driven by
  background progress snapshots, evaluated away from the audio callback.
- No dependency is added.
- A later slice can move button dispatch behind command ids without changing the
  visible top bar first.

## Follow-Ups

- Design imported clip media relink chooser flow.
- Move toward a longer-lived app command table when global shortcuts or command
  palette UI need it.
- Add command palette and shortcut editor UI only after focus and text-entry
  ownership are covered by tests.
