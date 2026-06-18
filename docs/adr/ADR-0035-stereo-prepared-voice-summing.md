<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0035: Stereo Prepared Voice Summing

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0034 introduced `TrackVoiceSchedule` as the prepared handoff from timeline
and track-mix planning into future rendering. The next vertical slice needs a
small render proof that can sum more than one prepared imported voice without
doing project reads, file I/O, decoding, schedule construction, or UI work in
the render loop.

The prototype still uses mono prepared PCM16 WAV buffers. Stereo and
multi-channel import, resampling, time-stretching, plugin processing, and track
automation remain later milestones.

## Decision

Extend `AudioEngineStub` with a prepared voice schedule playback mode.

The caller installs, before rendering:
- immutable prepared mono sample buffers keyed by imported clip id;
- a prebuilt `TrackVoiceSchedule` containing render offsets, clip-local offsets,
  frame counts, and left/right gain coefficients.

During rendering, the engine:
- walks only the prebuilt schedule and installed immutable buffers;
- treats missing buffers as silence;
- treats non-finite prepared samples as silence;
- multiplies each voice by its precomputed left/right gains;
- sums overlapping voices into stereo output;
- hard-clips each output channel to `[-1.0, 1.0]`.

Hard clipping is intentionally simple for v0.1. It gives deterministic behavior
for tests and prevents out-of-range samples from escaping the stub. A later
limiter or floating master bus can replace this policy behind the same schedule
shape.

## Consequences

- The project now has a testable multi-voice stereo render proof.
- The render loop does not construct schedules, read project state, perform file
  I/O, decode media, or call UI code.
- Overlapping imported clips can be summed once higher-level session/app code
  prepares all needed buffers for a render window.
- The app playback path now uses this multi-voice render mode for cached
  prepared voice windows, as recorded in ADR-0036.
- No new dependency is introduced.

## Follow-Ups

- Implement resampling before claiming production-quality playback for files
  whose sample rates differ from the output device, following ADR-0040.
- Replace hard clipping with a more musical limiter when the mixer milestone
  begins.
