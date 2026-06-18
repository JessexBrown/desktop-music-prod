<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0065: Imported Clip Inspector Edit Draft Model

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0064 defined the editable imported clip inspector flow before any visible
text fields were added. The app already has session commands for imported clip
placement and media replacement, plus undo/redo history for successful
non-no-op commits.

The next implementation step needs a plain C++ object that future JUCE controls
can use while the user is typing or selecting relink metadata. The draft state
must validate and cancel edits without mutating the project, because rejected
or cancelled text entry should not create undo history or disturb timeline
keyboard focus.

## Decision

Add `ImportedClipInspectorEditDraft` to `projectname_core`.

The draft is created from `ImportedClipInspectorState` and exposes three
availability states:

- `unavailable` when no imported clip is present;
- `readOnlyFallback` when the inspector is showing the first imported clip
  fallback rather than an explicitly selected clip;
- `editable` when the inspector state belongs to the selected imported clip.

Only the `editable` state can emit commit objects. The first commit objects are:

- `ImportedClipStartBeatCommit`, carrying `clipId` and finite non-negative
  `startBeats`;
- `ImportedClipMediaRelinkCommit`, carrying `clipId`, package-relative
  `relativePath`, optional package-relative `analysisPath`, and finite positive
  `lengthBeats`.

The draft validates user-editable values before the UI calls session commands.
Start-beat text is parsed with the classic C locale and must consume the whole
field. Media relink validation rejects empty media paths, package traversal,
absolute package paths, unsafe analysis paths, and invalid lengths. An empty
analysis path remains valid for a relink whose waveform analysis has not been
generated yet.

Cancel operations restore the last committed values held by the draft. They do
not call `AppSession`; if a future UI accidentally applies the restored value,
the existing app-session no-op behavior prevents undo history from being
recorded.

This slice does not add visible text fields, file chooser behavior, package
file copying/restoration, drag/drop placement, global shortcuts, or command
palette entries.

## Consequences

- Future inspector controls have a tested validation boundary before mutating
  project/session state.
- The fallback inspector remains discoverable but read-only.
- Invalid and cancelled edits do not produce commit objects.
- Undo history remains centralized in `AppSession` after a valid commit object
  is applied.
- The audio callback remains untouched.
- No dependency is added.

## Follow-Ups

- Design imported clip media relink chooser flow.
- Add a media relink chooser separately from package cleanup/restoration.
- Add inspector focus tests when visible text entry exists.
