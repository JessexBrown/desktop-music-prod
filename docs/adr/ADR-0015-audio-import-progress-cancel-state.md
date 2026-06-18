<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0015: Audio Import Progress and Cancel State

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0014 moved audio import work to a background job, but the app shell only
reported a generic importing status. Users need visible feedback during import
and a way to cancel before the project package is mutated.

The current importer is still a small first-party PCM16 WAV path. It should not
claim third-party codec support, sample-rate conversion, or waveform analysis
yet.

## Decision

Expose `BackgroundAudioImportProgress` from `BackgroundAudioImportJob`.

The progress snapshot contains:

- an import phase: pending, decoding, ready-to-commit, copying, committing,
  completed, failed, or cancelled;
- a coarse percent in the range 0-100;
- decoded and total frame counters during WAV decode;
- copied and total byte counters during staged file copy;
- whether cancellation was requested.

Split project audio import into decode and commit steps:

- `loadPcm16WavAsPreparedMonoClip` performs cancellable decode into a prepared
  mono buffer and reports frame progress;
- the background job checks cancellation during decode, after decode, and before
  package mutation;
- `commitPreparedAudioImportToProjectPackage` stages and commits the source
  audio into the package, appends the clip, saves the manifest, and applies the
  project only after success.

The JUCE shell displays non-modal progress in the status area and adds a Cancel
button that is enabled only before the job enters the commit phase.

The Win32 fallback import smoke and unit tests assert terminal progress states.

## Consequences

- Users get visible import progress and an explicit cancel action.
- Cancellation is honored before package mutation, including during decode,
  after decode, and during staged file copy.
- The current project remains unchanged on unsupported-file, missing-file, and
  cancelled imports.
- Once manifest commit begins, cancellation is disabled so the package transition
  remains simple and reviewable.
- The audio render callback remains untouched by import progress, cancellation,
  file I/O, decoding, and project mutation.

## Follow-Ups

- Add visible imported clips and waveform thumbnails in the arrangement view.
