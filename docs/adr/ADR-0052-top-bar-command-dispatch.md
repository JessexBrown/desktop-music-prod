<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0052: Top-Bar Command Dispatch

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0051 added stable app command ids and top-bar enabled state, but button
clicks still called implementation methods directly. That left command
execution outside the registry path described by ADR-0050.

The next slice needed to route the existing top-bar actions through command ids
without changing their visible behavior, adding global shortcuts, or introducing
command palette UI.

## Decision

Add a small JUCE-free `AppCommandDispatcher` and `AppCommandResult` model to
`projectname_core`.

The dispatcher:

- checks the current `AppCommandRegistry` before running a handler;
- returns `handled`, `handledWithStatus`, `disabled`, or `failed`;
- returns the registry disabled reason when a command is unavailable;
- rejects duplicate handlers;
- reports unknown command ids and missing handlers as recoverable failures.

`MainComponent` now maps the existing top-bar buttons to stable command ids and
dispatches them through app command handlers:

- `transport.play`;
- `transport.stop`;
- `project.save`;
- `project.open`;
- `audio.import`;
- `audio.import.cancel`;
- `timeline.preparation.cancel`;
- `audio.settings.show`.

The handlers still call the existing UI/session methods, so Play, Stop, Save,
Open, Import Audio, Cancel Import, Cancel Timeline Preparation, and Audio/MIDI
Settings keep their current layout, behavior, and status messages. Disabled or
failed command results are surfaced in the existing status line.

This ADR does not bind any keyboard shortcuts, add command palette UI, or move
audio-thread work into command handlers.

## Consequences

- Top-bar actions now exercise the same command id path future shortcuts and
  command-palette entries can reuse.
- Disabled-command behavior is testable without JUCE.
- The command result model has a status-line path before global shortcuts are
  introduced.
- The dispatcher is intentionally small and reconstructed by the UI layer for
  now; a longer-lived command table can be introduced when shortcut/palette work
  needs it.
- No dependency is added.

## Follow-Ups

- Add compact visible timeline viewport controls.
- Add command palette and shortcut editor UI only after text-entry/focus
  ownership is covered by tests.
- Add undo boundaries before editable inspector or timeline clip operations.
