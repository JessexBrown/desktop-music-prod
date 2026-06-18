<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0012: Project Package Audio Import

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0011 proved that a user-owned PCM16 WAV file can be decoded into a prepared
mono buffer outside the audio callback. The project also needs imported audio to
become part of a saved project package instead of remaining a transient playback
buffer.

The package direction in `GOAL.md` requires asset paths to be relative when
inside the package, and project loading should be resilient to missing future
assets. The first implementation should preserve reviewability and avoid adding a
large browser/import UI workflow before the core persistence behavior is stable.

## Decision

Add `ProjectAudioImport` to `projectname_core`.

The first import path:

- validates and decodes a source PCM16 WAV through `WavAudioImporter`;
- creates the project package `audio/` folder outside the audio callback;
- copies the source file into `audio/` with a sanitized unique filename;
- appends an `audio-file` clip to the first audio track, creating an imported
  audio track only if the project has no audio track;
- stores the copied asset as a manifest-relative path such as `audio/kick.wav`;
- computes a first-pass clip length in beats from source frame count, sample
  rate, and project tempo;
- saves the project package manifest after the copy and model update;
- returns the prepared mono buffer so callers can explicitly hand it to playback
  code.

Use a copied `ProjectModel` while preparing the mutation so failed imports do not
replace the current in-memory project. Do not add native file choosers, waveform
generation, resampling, stereo clip playback, background jobs, or drag/drop in
this step.

## Consequences

- Core tests can now prove that imported WAV files are copied into package
  storage and round-trip through `manifest.json`.
- Duplicate imports do not overwrite existing package audio files.
- Invalid WAV imports fail before project mutation or manifest writes.
- File I/O, decoding, allocation, and manifest writing remain outside the audio
  render path.
- User-facing import should stay aligned with ADR-0013 as UI behavior grows.

## Follow-Ups

- Keep the native app shell wired to `ProjectAudioImport` as project chooser
  behavior expands.
- Add background job handoff before importing larger user files.
- Add sample-rate conversion before user-facing import is considered complete.
- Add waveform thumbnails and stereo/multichannel clip playback.
