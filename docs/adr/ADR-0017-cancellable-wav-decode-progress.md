<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0017: Cancellable WAV Decode Progress

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0015 added import progress and cancellation around the background job, and
ADR-0016 staged package writes so cancelled copy work does not leave partial
assets. WAV decode itself still ran as one uninterrupted operation. Large source
files need frame-level progress and a cancellation check before the package
staging path begins.

## Decision

Add `WavDecodeOptions` and `WavDecodeProgress` to the first-party PCM16 WAV
importer.

The decoder now:

- accepts an optional atomic cancellation flag;
- reports decoded and total frame counts plus a percent;
- checks cancellation before opening the file, during the PCM frame loop, and
  after the frame loop before returning a prepared clip;
- returns a descriptive cancellation error without producing a partial
  `PreparedMonoAudioClip`;
- keeps the existing no-options loader as the simple call path.

`ProjectAudioImportOptions` carries the same cancel flag and a decode progress
callback so full project-package imports can cancel during decode before any
package folders, staged files, or manifests are created.

`BackgroundAudioImportJob` stores decoded frame counters in its progress
snapshot and maps decode progress into the import percent before staged copy
begins.

## Consequences

- User-facing import status can show decode frame progress and staged-copy byte
  progress as two different phases.
- Cancelling during decode leaves the live project unchanged and does not create
  package assets.
- The importer still supports only PCM16 WAV; other codecs and sample-rate
  conversion remain future work.
- Decode and progress callbacks run off the realtime audio callback path.

## Follow-Ups

- Waveform summary metadata is persisted by ADR-0018.
- Throttle progress callback frequency if profiling shows excessive UI churn on
  very large files.
- Add AIFF/FLAC/MP3 only after license/dependency review.
