<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0061: Imported Clip Placement Undo History

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0060 defined the first undo/redo boundary for imported clip edits. The
smallest implementation slice is timeline placement only because it changes
manifest state without touching package files or prepared audio contents.

`AppSession::setImportedAudioClipStartBeats` is already the session command
that future inspector or drag/drop UI should call for committed placement edits.

## Decision

Add a narrow imported-clip placement history to `AppSession`.

The session records successful non-no-op calls to
`setImportedAudioClipStartBeats` as `{ clipId, beforeStartBeats,
afterStartBeats }` entries. Failed validation and no-op moves are not recorded.
A successful new placement edit clears redo history.

Expose plain C++ session methods:

- `canUndoImportedClipPlacementEdit`;
- `canRedoImportedClipPlacementEdit`;
- `undoImportedClipPlacementEdit`;
- `redoImportedClipPlacementEdit`.

Undo and redo call `ProjectModel::setImportedAudioClipStartBeats` directly so
they do not recursively record another undo entry. If the clip id is stale,
missing, or no longer an imported audio clip, the operation fails with the
model's recoverable error and leaves project state plus history unchanged.

Project replacement and project load clear placement undo/redo history because
existing edit entries belong to the previous project graph.

This slice does not add visible undo/redo controls, global shortcuts, command
palette entries, media replacement undo, editable inspector fields, or timeline
drag/drop.

## Consequences

- Placement edits now have a tested undo/redo core path before UI edit gestures
  are introduced.
- Redo clearing behavior is established for the first edit stack.
- Placement undo/redo does not invalidate prepared playback cache because media
  content is unchanged.
- The audio callback remains untouched.
- No dependency is added.

## Follow-Ups

- Design package-local media quarantine and restore commands.
- Add visible undo/redo commands only after focus ownership and text-entry
  behavior are covered by tests.
