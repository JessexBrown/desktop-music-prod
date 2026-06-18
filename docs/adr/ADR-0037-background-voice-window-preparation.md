<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0037: Background Voice Window Preparation

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0036 routed cached overlapping imported clips through a prepared
multi-voice render window, but reopened projects usually have an empty prepared
audio cache. The background timeline preparation job still decoded only the
first relevant imported clip, so completion could fall back to single-clip
timeline playback instead of scheduling the full voice window.

The next increment needs to preserve the existing safety boundary: project
reads, package path validation, WAV decoding, cache lookup, and schedule
construction stay outside the audio callback. Completion must also reject stale
results if the current project changed while the background job was running.

## Decision

Extend `BackgroundTimelinePlaybackPreparationJob` so it asks `AppSession` for a
cached timeline voice window before decoding. When the session reports
`backgroundPreparationRequired`, the job decodes every missing imported clip in
that requested window and returns a batch of prepared clip buffers.

`completeBackgroundTimelinePlaybackPreparation` now:
- rejects cancellation before caching;
- validates each prepared clip against the current project through
  `AppSession::cacheImportedTimelineClip`;
- retries `AppSession::playCachedTimelineVoiceWindow` after all decoded buffers
  are cached;
- returns a `scheduledVoiceWindow` completion when the full window is ready.

The previous single-clip result remains in the background result for
compatibility with existing tests and fallback paths.

## Consequences

- Reopened projects with overlapping imported clips can prepare and schedule a
  complete cached voice window after background decoding.
- The audio callback still consumes only immutable prepared sample buffers and a
  prebuilt `TrackVoiceSchedule`.
- Partial or stale background results do not schedule playback.
- Progress remains phase/percent based and frame counts currently describe the
  active decoded file rather than a precomputed total across all files.
- No new dependency is introduced.

## Follow-Ups

- Implement resampling before claiming production-quality playback for files
  whose sample rates differ from the output device, following ADR-0040.
- Replace the fixed prepared window with a rolling handoff once streaming or
  longer arrangements are introduced.
