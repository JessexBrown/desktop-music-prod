<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0067: Imported Clip Media Relink Chooser Flow

## Status

Accepted for the v0.1 prototype.

## Context

Imported audio clips can now be selected, inspected, repositioned from the
inspector, and edited through app-session undo/redo. The plain C++ inspector
draft model can also validate media relink metadata, but there is not yet a
native chooser flow that turns a user-selected audio file into package-relative
`relativePath`, `analysisPath`, and `lengthBeats` values for
`AppSession::replaceImportedAudioClipMedia`.

The current project-package import path already has the important safety shape:
decode PCM16 WAV files away from the audio callback, stage file copies before
package mutation, write waveform summary metadata under `analysis/`, and report
recoverable progress/cancel state. Relinking should reuse that shape without
creating a new clip, new track, or full import side effect.

## Decision

The first media relink UI will be a selected-clip-only action from the imported
clip inspector. The first chooser accepts PCM16 WAV files only, matching the
current importer and avoiding unsupported format claims.

The chooser flow:

1. User selects an imported clip explicitly.
2. User invokes the inspector relink action.
3. A native open-file chooser asks for a `.wav` file.
4. The app starts a background relink preparation job with the selected clip id,
   project/package snapshot, source WAV path, and tempo snapshot.
5. The job decodes the WAV, calculates `lengthBeats`, stages a package audio
   copy, writes staged waveform summary metadata, and returns package-relative
   final paths plus the decoded length.
6. The UI verifies that the selected clip is still the intended imported clip.
7. The UI validates the returned metadata through
   `ImportedClipInspectorEditDraft::makeMediaRelinkCommit`.
8. A commit helper finalizes staged files into the project package and then
   calls `AppSession::replaceImportedAudioClipMedia`.

The first implementation always copies the chosen file into the project
package's `audio/` folder with a unique sanitized name and writes a matching
unique waveform summary under `analysis/`. It does not reuse or deduplicate
existing package files, even if the chosen file already lives in the package.

`lengthBeats` is calculated from decoded frame count, decoded sample rate, and
the tempo snapshot captured when the relink job starts. The relink does not
time-stretch or resample audio. If tempo later changes, the clip keeps the beat
length recorded by the relink commit, consistent with current imported clip
behavior.

Successful relinks preserve the clip id, clip name, track membership, selection,
and start beat. They update only `relativePath`, `analysisPath`, and
`lengthBeats`, and the existing app-session media replacement path invalidates
prepared playback cache entries for the clip.

Failure and cancellation rules:

- chooser cancellation does not mutate project state;
- decode, copy, analysis, validation, stale-selection, or commit failure reports
  recoverable status and leaves the project unchanged;
- staged files are cleaned up on cancellation, stale completion, or commit
  failure;
- successful relink does not delete old audio or analysis files;
- undo/redo changes manifest references only and relies on old files remaining
  available;
- unreferenced package cleanup/restoration remains a later explicit feature
  after ADR-0071 inventory and restoration boundaries.

The first relink commit does not automatically save the project manifest. It
changes the in-memory project and package assets, and the existing Save command
persists the manifest state. A later autosave/recovery feature can decide
whether relink commits should trigger immediate manifest writes.

This slice does not implement the chooser, background job, package commit
helper, cleanup UI, global shortcuts, command-palette entries, drag/drop relink,
AIFF/FLAC/MP3 support, resampling, or time-stretching.

## Consequences

- Relink design stays aligned with the package import staging model.
- Relink does not add any real-time audio-thread work.
- Users keep undoable manifest edits without destructive package cleanup.
- The next implementation can be a plain C++ preparation model with focused
  tests before visible chooser wiring.
- No dependency is added.

## Follow-Ups

- Add package media quarantine file-moving command.
