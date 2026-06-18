<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0040: Imported Audio Sample-Rate Policy

## Status

Accepted for the v0.1 prototype.

## Context

Imported PCM16 WAV clips already preserve their decoded source sample rate in
`PreparedMonoAudioClip`. Timeline playback plans, voice windows, and the audio
device run in output-device sample frames. When an imported clip's source sample
rate differs from the output device, the current render path would consume one
prepared source sample per output frame. That is deterministic, but it is not
correct sample-rate conversion.

The project needs an explicit v0.1 policy before multi-voice playback grows
further, without moving decoding, resampling, file I/O, allocation, or project
inspection into the audio callback.

## Decision

Do not implement resampling, time-stretching, or streaming in v0.1. Continue to
decode imported PCM16 WAV clips into immutable prepared mono sample buffers off
the audio thread, and continue to schedule timeline voice windows in output
sample frames.

When prepared timeline voice-window playback sees a cached imported clip whose
source sample rate differs from the normalized output sample rate by more than
one hertz, record a `TimelinePlaybackSampleRateMismatch` with:
- clip id;
- clip name;
- package-relative audio path;
- source sample rate;
- output sample rate.

The mismatch metadata is carried on `TimelineVoicePlaybackPreparation`, survives
background preparation completion, and is surfaced by the JUCE app status text as
a clear warning that resampling is not implemented yet.

This policy does not reject playback, alter prepared buffers, mutate project
files, or add fields to the audio-callback-facing `PreparedTrackVoiceBuffer`.
The audio callback still consumes only immutable sample buffers and prepared
voice schedules.

## Consequences

- Users get an honest warning when multi-voice prepared playback includes clips
  whose source rate differs from the current output device.
- Tests can verify mismatch metadata propagation through cache-hit and
  background-preparation completion paths without adding DSP complexity.
- Playback remains deterministic but may play mismatched clips at the wrong
  speed/pitch until resampling lands.
- No new dependency is introduced.

## Follow-Ups

- Add a real resampling stage outside the audio callback before claiming
  production-quality imported audio playback.
- Expand from the ADR-0041 first-clip inspector warning to selected timeline
  clips once clip selection exists.
- Revisit cache accounting if future resampling stores converted buffers in
  addition to source-rate buffers.
