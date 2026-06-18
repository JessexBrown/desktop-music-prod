<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0036: Session Prepared Voice Window Playback

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0034 defined timeline voice schedules and ADR-0035 proved that
`AudioEngineStub` can sum those voices into stereo output from immutable prepared
buffers. The remaining gap was the app/session boundary: pressing Play still
scheduled one imported clip at a time even when multiple recently imported clips
were already cached.

The v0.1 path must keep decoding, cache lookup, project reads, and schedule
construction outside the audio callback. It also must stay small: no streaming,
time-stretching, plugin processing, or resampling policy in this change.

## Decision

Add `AppSession::playCachedTimelineVoiceWindow`.

The method:
- maps the current transport beat position to timeline samples;
- builds the imported-audio timeline plan from the current project;
- finds the active or next imported clip to define a bounded playback window;
- expands that window enough to include the selected clip from the current
  transport position;
- builds a `TrackVoiceSchedule` for every imported clip overlapping the window;
- gathers the required immutable prepared buffers from the session cache;
- returns `voiceWindowReady`, `backgroundPreparationRequired`, or
  `generatedToneFallback`.

The JUCE app now tries this cached voice-window path before falling back to the
older single-clip timeline preparation path. `AudioDeviceService` installs the
prepared buffers and schedule on `AudioEngineStub` outside the callback, while
the callback only renders the prebuilt engine state.

## Consequences

- Pressing Play can route cached overlapping imported clips through the
  multi-voice stereo render path.
- The render callback still performs no project reads, decoding, file I/O,
  cache lookup, UI calls, logging, or schedule construction.
- Cache misses are surfaced as a background-preparation requirement with the
  missing clip plans identified.
- Background preparation can now decode every missing imported clip in a
  requested voice window, as recorded in ADR-0037.
- Resampling is still not implemented. ADR-0040 adds warning metadata for clips
  whose source sample rate differs from the output device.

## Follow-Ups

- Implement resampling before claiming production-quality playback for files
  whose sample rates differ from the output device.
- Replace the fixed prepared window with a rolling handoff once streaming or
  longer arrangements are introduced.
