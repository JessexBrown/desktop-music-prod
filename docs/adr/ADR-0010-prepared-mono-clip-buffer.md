<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0010: Prepared Mono Clip Buffer

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0009 introduced sample-position scheduling for a generated clip. The next
timeline step is to prove that the render path can read prepared audio sample
data, not only synthesize tone samples.

File import, decoding, resampling, waveform thumbnails, and asset-copy workflows
are larger Milestone 3 tasks. They should not be mixed into the first render
buffer proof.

## Decision

Add a prepared mono clip buffer to `AudioEngineStub`:

- buffer samples are supplied outside the render path;
- samples are clamped to `[-1.0, 1.0]` and non-finite values become silence
  during preparation;
- the prepared clip can be scheduled by timeline start sample;
- rendering outputs silence before and after the scheduled range;
- rendering fans the mono sample out to every requested output channel;
- seeking into the scheduled range uses the correct clip-local buffer offset.

Keep the render path allocation-free and lock-free. This is not a decoder, mixer,
sample-rate converter, waveform cache, or multitrack timeline engine.

## Consequences

- Core tests can now prove timeline playback of known prepared sample data.
- File import can target a small prepared-buffer contract before expanding into
  asset management and waveform UI.
- The first implementation is mono only and supports one scheduled prepared clip.

## Follow-Ups

- Connect decoded audio file import to project assets and UI controls.
- Add stereo and multichannel prepared buffer support.
- Replace the single-clip stub with an immutable render snapshot.
