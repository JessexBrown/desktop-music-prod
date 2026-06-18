<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0050: App Command Registry Design

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0048 defined the boundary between focused workspace commands and future
app-global commands. ADR-0049 added a tiny tested router for local workspace
shortcuts. The next step is to define the app-level command registry before
adding global shortcuts, a command palette, or shortcut editor UI.

The registry must preserve focus ownership, avoid stealing text-entry behavior,
and keep command execution out of the real-time audio callback. It should support
the existing explicit top-bar actions first, then grow toward discoverable
commands and user-editable shortcuts.

## Decision

Use stable string command ids with dotted namespaces:
- `transport.play`;
- `transport.stop`;
- `project.save`;
- `project.open`;
- `audio.import`;
- `audio.import.cancel`;
- `timeline.preparation.cancel`;
- `audio.settings.show`;
- `workspace.focus`;
- `commands.palette.show`.

Command metadata should include:
- id;
- user-visible label;
- short description;
- scope: `focused-surface`, `project`, `transport`, `audio-device`, or `app`;
- whether it is currently enabled;
- optional status text for disabled commands;
- a handler used by the UI/session layer.

Command execution should return a small result:
- `handled` for success without a special message;
- `handledWithStatus` plus human-readable status text;
- `disabled` plus reason;
- `failed` plus recoverable error text.

Routing order:
1. Native text entry, modal dialogs, and system components keep their platform key
   behavior first.
2. Focused surfaces route local commands first, using helpers like
   `WorkspaceCommandRouter`.
3. The app command registry handles global shortcuts and command-palette
   invocation only when the focused surface did not handle the input.

Enabled/disabled state must be evaluated from the UI/session thread. Command
handlers may enqueue background jobs or update app/session state, but they must
not run on the audio callback or perform unsafe real-time work.

The first candidate app-global commands are the actions already exposed by
buttons:
- Play;
- Stop;
- Save;
- Open;
- Import Audio;
- Cancel Import;
- Cancel Timeline Preparation;
- Show Audio/MIDI Settings.

This ADR does not bind any new shortcuts and does not add command palette or
shortcut editor UI.

## Consequences

- Future global shortcuts have a documented registry shape before behavior is
  added.
- Focused workspace shortcuts remain first-class and local.
- Top-bar commands can later move behind command ids without changing their
  visible UI.
- Command results create a consistent path for status-line messages and
  recoverable errors.
- No audio callback behavior changes.
- No new dependency is introduced.

## Follow-Ups

- Design package-local media quarantine and restore commands.
