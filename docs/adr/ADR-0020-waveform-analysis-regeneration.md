<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0020: Waveform Analysis Regeneration

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0018 writes imported-audio waveform summaries into the package `analysis/`
folder, and ADR-0019 draws thumbnails from those summaries. Project load must
still tolerate missing, deleted, or malformed analysis files. The recovery path
needs to stay outside the realtime audio callback and avoid making project open
depend on optional analysis metadata.

The project currently imports dependency-free PCM16 WAV files into package
audio. Regeneration can reuse that decoder while leaving broader format support
for later milestones.

## Decision

Add a shared waveform analysis regeneration boundary to `projectname_core`.

The regenerator:

- finds the first imported `audio-file` clip with package audio;
- validates package-relative audio and analysis paths before touching disk;
- returns `notNeeded` when the existing summary is present and valid;
- decodes packaged PCM16 WAV audio with the cancellable WAV decode path when
  the summary is missing or invalid;
- writes a replacement summary under the existing package `analysis/` path;
- reports explicit status values for no imported audio, missing paths, unsafe
  paths, missing audio, decode failure, cancellation, and save failure;
- does not mutate the project manifest.

Add `BackgroundWaveformAnalysisJob` for app/UI callers. The job owns a copy of
the project snapshot, runs the regenerator on a worker future, exposes cancel
and readiness state, and waits during destruction if work was started.

The Windows fallback import smoke now removes the generated summary and verifies
that the background regeneration path restores it. Unit tests cover successful
regeneration, invalid-summary recovery, pre-start cancellation, and cancellation
during decode progress.

## Consequences

- Project load and thumbnail drawing remain tolerant of optional analysis files.
- Missing or invalid summaries can be repaired without decoding audio in paint
  code or on the audio callback.
- The project manifest is not rewritten during regeneration, so future stale
  summary detection can decide whether metadata changes belong in a save
  transaction.
- The first implementation regenerates the first imported audio clip only; lane
  and multi-clip work should expand this boundary once the UI can display more
  than one clip.
- No new dependency is introduced.

## Follow-Ups

- Extend regeneration across all imported clips now that the timeline lane can
  surface multiple per-clip analysis states.
- Add stale-summary detection using source file metadata or a summary input
  fingerprint.
- Surface regeneration status in the app UI instead of only using tests and
  smoke paths.
