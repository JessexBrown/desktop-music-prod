<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0033: Timeline Preparation Progress and Cancel UI

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0029 moved imported timeline playback cache misses onto a background
preparation job. The job already reported phase, percent, frame counts, and
cancellation state, but the native app only surfaced a narrow decoding status
and had no non-modal way to cancel long timeline preparation.

The app needs to stay responsive when a reopened project prepares imported audio
for playback, and cancelled or stale worker results must not schedule audio
after the user stops playback or changes the project.

## Decision

Expose timeline preparation progress in the JUCE app status area for pending,
planning, decoding, completed, failed, and cancelled states. Add a top-bar
`Cancel Prep` action that requests cancellation on the running background
timeline preparation job without touching the audio callback.

Add `completeBackgroundTimelinePlaybackPreparation` in the core layer. The JUCE
app uses this completion gate after a worker finishes. The helper:
- returns cancelled before inspecting ready-looking prepared samples;
- rejects prepared results whose clip id and media path no longer match the
  current session project;
- caches and re-resolves matching prepared results before scheduling;
- falls back to generated tone through the existing session path when
  preparation fails.

Pressing Stop while timeline preparation is running also requests cancellation,
so an old worker result cannot start playback after the user has stopped.

## Consequences

- Long timeline preparation has visible progress and a non-modal cancel action.
- Cancelled preparation does not cache or schedule prepared samples, even if the
  worker result contains ready-looking audio.
- Stale media-path results are rejected through tested core code instead of
  being handled ad hoc in the app component.
- The audio callback remains unchanged and only renders immutable prepared
  buffers installed before scheduling.
- No new dependency is introduced.

## Follow-Ups

- Add screenshot or UI smoke coverage for the progress/cancel control once the
  JUCE app target is buildable in CI.
