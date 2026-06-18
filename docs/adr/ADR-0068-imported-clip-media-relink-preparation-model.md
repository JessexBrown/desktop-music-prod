<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0068: Imported Clip Media Relink Preparation Model

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0067 defined the selected-clip media relink chooser flow. Before adding a
visible native chooser button, the project needs a plain C++ preparation
boundary that can decode a user-selected PCM16 WAV file, stage package assets,
and produce metadata that the inspector edit draft can validate.

The first relink implementation must not create a new clip or track, mutate the
project during preparation, touch the real-time audio callback, or delete old
package assets. Stale UI completions must be recoverable because the selected
clip can change while a future background preparation job is running.

## Decision

Add `ImportedClipMediaRelink` to `projectname_core`.

The preparation request carries:

- a project snapshot;
- the project package directory;
- source PCM16 WAV path;
- the selected imported clip id;
- optional cancellation and progress callbacks.

`prepareImportedClipMediaRelink` validates that the selected clip id still
matches the project selection and identifies an imported audio clip. It then
decodes the source WAV, calculates `lengthBeats` from the project tempo
snapshot, stages an audio copy under `.projectname-staging/`, stages waveform
summary metadata, and returns:

- selected clip id;
- package-relative `audio/...` path;
- package-relative `analysis/...` path;
- finite positive `lengthBeats`;
- staged and final filesystem paths;
- the decoded prepared mono clip for future cache handoff.

`commitPreparedImportedClipMediaRelink` is still a plain C++ helper, not a UI
action. It verifies that the same imported clip remains selected in
`AppSession`, moves staged files into final package locations, calls
`AppSession::replaceImportedAudioClipMedia`, and removes the staging directory.
If selection is stale, package commit fails, or the session rejects the edit,
the helper cleans staged files and reports a recoverable status.

Successful commits preserve clip id, clip name, track membership, selection,
and start beat. They update only media path, analysis path, and length through
the existing app-session media replacement path, so undo history and prepared
cache invalidation remain centralized there.

This slice does not add a visible relink button, native file chooser wiring,
background job wrapper, command-palette entry, global shortcut, package
cleanup/restoration UI, AIFF/FLAC/MP3 support, resampling, or time-stretching.

## Consequences

- The future chooser has a tested core boundary before UI wiring.
- Invalid WAV sources fail before staging package files.
- Cancellation and stale-selection paths clean staging directories.
- Generated metadata is compatible with `ImportedClipInspectorEditDraft`.
- The audio callback remains untouched.
- No dependency is added.

## Follow-Ups

- Design package-local media quarantine and restore commands.
