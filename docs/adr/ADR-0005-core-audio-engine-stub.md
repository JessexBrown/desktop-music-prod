<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0005: Core Audio Engine Stub

## Status

Accepted for the v0.1 prototype.

## Context

The first vertical slice needs generated audio that can be exercised by both the
JUCE desktop app and dependency-light verification targets. The project also
needs a small seam between UI/device code and future DAW engine work so transport,
project, and rendering tests can grow without depending on a full timeline or
plugin host implementation.

Real-time audio rules still apply: callback rendering must not allocate, lock,
log, perform file I/O, parse project files, scan plugins, or call UI code.

## Decision

Add `AudioEngineStub` to the plain C++ `projectname_core` library. It owns the
generated-tone renderer and exposes a minimal rendering boundary for:

- preparing sample rate state before playback;
- enabling or disabling generated tone playback;
- starting a duration-bounded generated clip for smoke tests and future timeline
  clip work;
- scheduling a generated clip by timeline sample start and duration;
- preparing and scheduling a mono audio sample buffer by timeline sample start;
- rendering float output buffers for the JUCE audio device callback;
- rendering interleaved 16-bit PCM buffers for the Win32 fallback smoke path.

Keep the stub intentionally narrow. It is not a timeline engine, clip scheduler,
decoder, plugin graph, mixer, or offline renderer yet.

## Consequences

- The JUCE app and Win32 fallback app now share the same core-generated audio
  proof instead of each constructing oscillator output separately.
- The Win32 fallback smoke path renders a prepared generated clip buffer through
  the engine stub.
- Unit tests can verify engine-level play/stop/render/end-of-clip behavior
  without a live device.
- Scheduled generated-clip tests can verify start offsets and timeline position
  behavior without a full clip engine.
- Prepared-buffer tests can verify known sample playback without adding file
  decoding, waveform generation, or asset import in the same step.
- The future timeline/audio-clip engine has a clear place to grow while preserving
  the real-time safety rules.
- The stub is not thread-safe as a general mutable object. Device-facing code must
  continue using explicit handoff points such as atomics when UI state controls
  audio callback rendering.

## Follow-Ups

- Replace generated-tone-only rendering with a prepared render graph snapshot.
- Connect decoded audio clip sources to project assets before implementing
  file-backed timeline clips.
- Keep plugin scanning and project parsing outside this render boundary.
