<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0029: Background Timeline Playback Preparation

## Status

Accepted for the v0.1 prototype. Extended by ADR-0033, which surfaces progress
and cancellation in the native app, and ADR-0037, which prepares every missing
clip in a requested voice window.

## Context

ADR-0028 added a prepared-buffer cache so imported clips can play from the
timeline without decoding again after import. Reopened projects, cleared caches,
and clips evicted from the bounded cache recorded in ADR-0031 still need a way
to prepare the copied package WAV without blocking the Play button handler.

Preparation can perform file I/O, WAV parsing, allocation, and cache mutation.
Those operations must never happen on the audio callback thread, and for the UI
they should happen outside the immediate Play interaction when possible.

## Decision

Add `BackgroundTimelinePlaybackPreparationJob`.

The job:

- takes a snapshot of the project, package directory, output sample rate, and
  current transport position embedded in the project transport;
- builds the timeline playback plan on a worker;
- decodes the selected package PCM16 WAV on that worker;
- reports planning/decoding/completed/failed/cancelled progress;
- returns the same `TimelinePlaybackPreparation` shape used by `AppSession`;
- treats cancellation as a terminal state with a readable error.

The JUCE Play path now:

- first asks `AppSession` for cached timeline playback;
- schedules immediately when cached samples are ready;
- starts the background preparation job when the selected clip is uncached;
- polls from the UI timer;
- caches and schedules the prepared result if it still matches the current
  session;
- falls back to generated tone with a readable status on failure.

The audio callback remains unchanged: it only renders from a prepared buffer
installed before scheduling.

## Consequences

- Play no longer decodes uncached imported package audio in the button handler.
- Reopened projects can prepare timeline audio asynchronously.
- Missing media and cancellation are covered by core tests.
- Stale app results are rejected when the prepared clip no longer matches the
  current session cache constraints.
- The prototype still does not stream long arrangements or resample mismatched
  clip/device rates.
- No new dependency is introduced.

## Follow-Ups

- Revisit prepared-cache policy when streaming and multi-clip mixing land.
- Add screenshot or UI smoke coverage for timeline preparation progress once the
  JUCE app target is buildable in CI.
