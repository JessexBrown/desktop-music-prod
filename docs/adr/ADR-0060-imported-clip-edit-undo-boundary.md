<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0060: Imported Clip Edit Undo Boundary

## Status

Accepted for the v0.1 prototype.

## Context

Imported audio clips can now be selected, inspected, placed on the timeline,
relinked through model/session media replacement, and navigated with compact
viewport controls. The next UX step will make inspector fields or timeline
gestures editable. Those edits must not ship as one-way mutations.

The current domain commands already define the first edit surface:

- `ProjectModel::setImportedAudioClipStartBeats` changes clip placement in the
  manifest model only.
- `ProjectModel::replaceImportedAudioClipMedia` changes package-relative media
  and analysis references plus clip length.
- `AppSession::replaceImportedAudioClipMedia` additionally invalidates prepared
  playback cache entries for the edited clip.

Undo/redo must preserve those boundaries, avoid file I/O on the audio callback,
and stay testable without JUCE before visible edit controls are added.

## Decision

The first undo system will live in the plain C++ app/session domain, not inside
JUCE components.

Imported clip edit commands will be recorded as explicit completed user actions,
not as every pointer movement or every preview state. A drag/drop move may show
a temporary preview in UI state, but it records one undoable placement command
only when the gesture commits. Inspector field edits similarly record only after
the value is accepted.

The first undoable operations are:

- imported clip placement: old and new `startBeats`;
- imported clip media replacement: old and new `relativePath`, `analysisPath`,
  and `lengthBeats`.

The first undo stack does not include:

- selection changes;
- viewport pan/zoom/fit/center actions;
- transport changes;
- project save/load;
- audio import package copying or file deletion;
- plugin actions.

Undo and redo must route through `AppSession` commands rather than mutating
`ProjectModel` directly when session-side effects exist. Media replacement undo
and redo must therefore invalidate prepared cache entries just like the forward
replace command. Placement undo and redo do not invalidate prepared audio
because the clip content is unchanged.

Undo entries should store the clip id plus before/after values. If the clip is
missing or no longer an imported audio clip when an undo/redo is requested, the
operation fails with a recoverable error and leaves the project unchanged. A
successful new edit clears the redo stack. Failed edits and no-op edits are not
recorded.

## Consequences

- Editable inspector fields and future timeline drag/drop have a tested undo
  contract before they mutate project state.
- The audio thread remains untouched; undo/redo runs from UI/session paths.
- Media replacement cache invalidation remains centralized in `AppSession`.
- Package files are not copied, deleted, or restored by the first undo slice;
  undo changes manifest references only.
- No dependency is added.

## Required Tests Before UI Edits

- Placement edit command records old/new starts, applies redo, applies undo, and
  preserves prepared audio cache validity.
- Media replacement edit command records old/new media references and length,
  applies undo/redo through `AppSession`, and invalidates stale prepared cache
  entries on both directions.
- Failed validation does not record an undo entry.
- No-op edits do not record an undo entry.
- A new successful edit clears redo history.
- Missing/stale clip ids fail safely and leave project state unchanged.

## Follow-Ups

- Add visible undo/redo commands only after focus ownership and text-entry
  behavior are covered by tests.
