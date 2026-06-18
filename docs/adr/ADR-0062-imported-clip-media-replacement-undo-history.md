<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0062: Imported Clip Media Replacement Undo History

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0060 defines imported clip media replacement as one of the first undoable
edit surfaces. ADR-0061 added placement undo/redo, but media replacement has an
extra session-side requirement: prepared playback cache entries for the edited
clip must be invalidated whenever the media reference is changed, undone, or
redone.

`ProjectModel::replaceImportedAudioClipMedia` only changes manifest state.
`AppSession::replaceImportedAudioClipMedia` is the boundary that also clears
prepared timeline cache entries for the edited clip.

## Decision

Extend the `AppSession` imported-clip edit history to store typed entries:

- placement edits store old/new `startBeats`;
- media replacement edits store old/new `relativePath`, `analysisPath`, and
  `lengthBeats`.

Expose media-replacement-specific plain C++ session methods:

- `canUndoImportedClipMediaReplacementEdit`;
- `canRedoImportedClipMediaReplacementEdit`;
- `undoImportedClipMediaReplacementEdit`;
- `redoImportedClipMediaReplacementEdit`.

Media replacement undo and redo apply the stored manifest state through the
same session boundary that invalidates prepared playback cache for the clip. If
the clip id is stale, missing, or no longer an imported audio clip, undo/redo
fails with a recoverable model error and leaves project state plus history
unchanged.

Successful non-no-op media replacements are recorded, invalidate prepared cache
for the edited clip, and clear redo history. Failed validation and no-op
replacements are not recorded; no-op replacements also leave prepared cache
entries intact. Project replacement and project load clear imported-clip edit
history because entries belong to the previous project graph.

This slice does not copy, delete, or restore package files. Undo and redo only
switch manifest references. It also does not add visible undo/redo controls,
global shortcuts, editable inspector fields, or timeline drag/drop.

## Consequences

- Imported clip media relinks now have a tested undo/redo core path before
  editable inspector fields are introduced.
- Cache invalidation remains centralized in `AppSession`.
- Same-path relinks that only change analysis metadata or length still clear
  stale prepared buffers on forward edit, undo, and redo.
- The audio callback remains untouched.
- No dependency is added.

## Follow-Ups

- Add imported clip media relink preparation model.
- Add editable inspector or timeline gesture commits only after visible commands
  and text-entry behavior are covered by tests.
