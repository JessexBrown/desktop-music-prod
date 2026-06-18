<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0026: Imported Clip Timeline Playback Plan

## Status

Accepted for the v0.1 prototype.

## Context

Imported PCM16 WAV clips can be decoded into prepared mono buffers and placed on
the beat-based timeline. The audio stub could already render a prepared buffer
from a scheduled sample position, but there was no domain object that translated
project clip timing into sample ranges at the current tempo.

That translation must stay outside the realtime audio callback. It must not read
project packages, decode audio, allocate render buffers, inspect the UI, or touch
plugin state from the audio thread.

## Decision

Add `TimelinePlaybackPlan` as a plain C++ domain planner for imported
`audio-file` clips.

The planner:

- reads the already-loaded `ProjectModel`;
- uses the project transport tempo and a supplied output sample rate;
- filters out generated clips and invalid imported clip timing;
- converts each imported clip's `startBeats` and `lengthBeats` to timeline
  sample ranges;
- returns activation data for future clip entry or active transport positions,
  including the clip-local sample offset.

Extend `AudioEngineStub` with a prepared-buffer segment overload:

```cpp
startScheduledPreparedMonoClip(timelinePlaybackStartSample,
                               clipLocalStartOffsetSamples,
                               clipLengthSamples)
```

The overload is configured off the audio callback, then the callback only reads
the already-prepared sample vector and advances integer counters. Existing
single-buffer scheduling still works through the old overload.

## Consequences

- Tests now cover beat-to-sample planning, pre-roll silence, clip entry, seek
  into a clip, loop wrap into a clip, and post-clip silence.
- The first playback plan is intentionally single-buffer oriented. It does not
  mix overlapping clips, load package audio on demand, or time-stretch audio to
  tempo changes.
- ADR-0027 wires the plan into the app/session Play command for the first
  relevant imported clip.
- No new dependency is introduced.

## Follow-Ups

- Move app Play preparation to a background job or explicit prepared buffer
  cache.
- Add multi-clip track mixing and voice management.
- Decide how tempo changes should affect audio clips before implementing
  stretching or resampling behavior.
