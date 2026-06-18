<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0063: Imported Clip Edit Undo Command Routing

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0061 and ADR-0062 added plain C++ undo/redo history for imported clip
placement and media replacement. Those operations were callable directly on
`AppSession`, but the app command registry did not yet describe undo/redo
availability or dispatch.

Future visible controls, shortcuts, and command-palette entries need one stable
command route that respects the top undo/redo stack entry without binding UI
shortcuts before focus and text-entry behavior are designed.

## Decision

Add app command ids:

- `edit.undo`;
- `edit.redo`.

Extend `AppSession` with a public imported-clip edit command surface:

- `getNextImportedClipUndoEditKind`;
- `getNextImportedClipRedoEditKind`;
- `canUndoImportedClipEdit`;
- `canRedoImportedClipEdit`;
- `undoImportedClipEdit`;
- `redoImportedClipEdit`.

The generic undo/redo methods dispatch the current top imported-clip edit,
whether it is placement or media replacement. Type-specific methods remain for
focused tests and future UI that needs exact edit-kind enablement.

`AppCommandAvailability` now carries imported-clip undo/redo enablement, and the
prototype app command registry exposes disabled reasons when no matching edit is
available. `MainComponent` registers handlers for `edit.undo` and `edit.redo`
but does not add visible buttons, global shortcuts, command-palette UI, editable
inspector fields, drag/drop edits, or package file restoration.

## Consequences

- Imported clip edit undo/redo can now be routed through the same command
  metadata and dispatcher path as existing top-bar commands.
- The command layer handles top-of-stack placement and media replacement edits
  without knowing their storage details.
- Future visible controls can bind to stable command ids instead of calling
  type-specific session methods directly.
- The audio callback remains untouched.
- No dependency is added.

## Follow-Ups

- Design imported clip media relink chooser flow.
- Add visible undo/redo controls only after focus ownership and text-entry
  behavior are covered by tests.
