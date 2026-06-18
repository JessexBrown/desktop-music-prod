<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0018: Waveform Summary Analysis Metadata

## Status

Accepted for the v0.1 prototype.

## Context

Milestone 3 calls for waveform thumbnails for imported audio. The project does
not need a full waveform renderer yet, but it does need a durable package
artifact that future UI code can load without decoding the audio file again.

ADR-0016 already created staged package writes for imported audio, and ADR-0017
made PCM16 WAV decode cancellable with frame progress. Waveform analysis should
reuse the prepared mono buffer already produced by decode and stay off the
realtime audio callback path.

## Decision

Add `WaveformSummary` to `projectname_core`.

For each imported PCM16 WAV clip:

- compute a bounded peak/RMS bucket summary from the prepared mono samples;
- write the summary as human-readable JSON under the package `analysis/`
  folder;
- store the package-relative analysis path in `ProjectClip::analysisPath`;
- keep older manifests valid by treating missing `analysisPath` as empty;
- stage the analysis file before committing package changes;
- remove committed analysis files if clip attachment or manifest save fails.

The waveform summary JSON includes:

- format version;
- sample rate;
- source frame count;
- source frames per bucket;
- an array of peak/RMS buckets.

## Consequences

- UI code can draw a basic waveform thumbnail without decoding the source audio
  again, as recorded in ADR-0019.
- Project load remains tolerant of missing analysis files; the manifest model
  keeps the package-relative path and can regenerate analysis later.
- No new dependency is introduced; the existing nlohmann/json dependency writes
  the summary.
- The audio callback remains free of analysis work, file I/O, allocation, and UI
  calls.

## Follow-Ups

- Missing or invalid summary regeneration is recorded in ADR-0020; add
  stale-summary detection once source metadata is richer.
- Add multi-channel and resampled waveform summaries once import supports those
  paths.
