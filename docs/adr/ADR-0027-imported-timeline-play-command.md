<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0027: Imported Timeline Play Command

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0026 introduced a domain playback plan that maps imported audio clip beats
to output timeline samples. The JUCE Play button still needed to use that plan
instead of always enabling the generated test tone.

The first integration must preserve realtime safety. Preparing imported audio
can decode a project WAV file and allocate a sample buffer, so it must happen
before scheduling playback and outside the audio callback.

## Decision

Add an `AppSession::playFromTimeline` command.

The command:

- builds the imported-audio playback plan from the current project transport
  tempo and output sample rate;
- finds the first imported clip at or after the current transport sample;
- resolves the clip's package-relative path safely;
- decodes the PCM16 WAV into a prepared mono buffer outside the audio callback;
- returns a `TimelinePlaybackPreparation` with prepared samples, clip metadata,
  current transport sample, and timeline activation offsets;
- falls back to generated-tone playback when no imported clip can be prepared.

The JUCE audio service gets a timeline scheduling method that removes the audio
callback while swapping the prepared buffer, sets the engine's transport sample,
and schedules the prepared mono clip from the returned activation. The callback
continues to only render from an already-prepared buffer and advance counters.

The first implementation intentionally schedules only the first relevant
imported clip. It does not mix overlapping clips, resample mismatched clip/device
sample rates, or prepare playback on a background worker.

## Consequences

- Pressing Play can now schedule an imported PCM16 WAV clip from before the clip,
  from inside the clip, or after a loop wrap into the clip.
- The generated tone remains the audible fallback when the project has no
  schedulable imported clip or the clip cannot be prepared.
- ADR-0028 adds the first prepared-buffer cache so recently imported clips can
  play without decoding again on Play. ADR-0031 expands that cache to a bounded
  multi-entry cache. ADR-0036 through ADR-0040 extend this path to prepared
  multi-voice windows, persisted static track mix state, and sample-rate
  mismatch warnings.
- No new dependency is introduced.

## Follow-Ups

- Implement resampling after the warning-only sample-rate policy in ADR-0040.
