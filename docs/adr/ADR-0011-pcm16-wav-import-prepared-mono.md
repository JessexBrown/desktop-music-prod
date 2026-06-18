<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0011: PCM16 WAV Import to Prepared Mono Buffers

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0010 added a prepared mono clip buffer so the audio render path can play
known sample data without decoding files inside the callback. Milestone 3 needs
audio clip import, but a complete asset pipeline, waveform cache, resampling
policy, and UI workflow would be too broad for the next vertical step.

The project needs a legally clean, dependency-light proof that a user-owned audio
file can be decoded outside the audio thread and handed to the existing prepared
buffer playback path.

## Decision

Add `WavAudioImporter` to `projectname_core`.

The first importer supports RIFF/WAVE files containing 16-bit PCM sample data:

- file I/O, validation, decoding, vector allocation, and mono downmix happen
  before playback and outside the audio callback;
- imported mono samples use the existing prepared-buffer contract consumed by
  `AudioEngineStub`;
- multi-channel source files are averaged to mono for this first step;
- the importer records source sample rate, source channel count, frame count, and
  prepared mono samples;
- unsupported files return descriptive errors instead of partially prepared
  buffers.

Do not add a third-party decoder dependency yet. Do not add AIFF, FLAC, MP3,
resampling, waveform rendering, project asset-copy policy, or UI import controls
in this step.

## Consequences

- The core now has a deterministic, testable audio-file import proof.
- The audio callback remains free of file I/O, parsing, allocation, locks,
  logging, and UI calls.
- Tests synthesize tiny WAV files, import them, schedule the prepared buffer, and
  verify clip start offsets, stop/restart behavior, and end-of-clip silence.
- Imported audio is mono only and assumes the project/device sample rate already
  matches the source file.

## Follow-Ups

- Keep project-package import behavior aligned with ADR-0012.
- Add UI import controls for imported audio clips.
- Add sample-rate conversion before user-facing import is considered complete.
- Add stereo and multichannel prepared-buffer playback.
