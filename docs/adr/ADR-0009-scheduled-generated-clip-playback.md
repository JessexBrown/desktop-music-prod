<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0009: Scheduled Generated Clip Playback

## Status

Accepted for the v0.1 prototype.

## Context

The first audio engine stub could render a continuous generated tone or a
duration-bounded generated clip. Milestone 3 will eventually require real audio
clip playback on a timeline, but adding file import, decoding, waveform
generation, and a full scheduler at once would violate the current vertical-slice
discipline.

The project still needs an engine-level proof that clips can start at a timeline
sample position and render silence outside their active range without touching UI
or project files from the audio render path.

## Decision

Extend `AudioEngineStub` with one scheduled generated clip:

- caller sets a timeline sample position;
- caller starts a generated clip with a start sample and length in samples;
- rendering outputs silence before the clip start;
- rendering outputs generated tone during the clip region;
- rendering outputs silence and stops after the clip end;
- tests cover start offsets, seeking into a clip, stop behavior, and end silence.

Keep the implementation scalar and allocation-free inside rendering. This remains
a stub, not a general timeline graph, decoder, mixer, or plugin host.

## Consequences

- The core can now test sample-position-aware clip scheduling without live audio
  hardware.
- The future timeline engine has a narrow behavior contract to preserve when real
  decoded or prepared audio clips replace the generated source.
- Only one generated clip is supported in this prototype step.
- ADR-0010 extends this scheduling proof to a prepared mono sample buffer.

## Follow-Ups

- Replace the single scheduled generated clip with a prepared render snapshot.
- Add decoded audio buffer sources outside the realtime render path.
- Add track gain/pan/mute/solo after scheduling and file playback are stable.
