<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0034: Track Voice Scheduling and Mixing Boundary

## Status

Accepted for the v0.1 prototype. Extended by ADR-0035 through ADR-0040, which
add stereo prepared voice summing, app-session voice-window routing, background
voice-window preparation, persisted static track mix state, and the first static
mixer UI controls, plus sample-rate mismatch warning metadata.

## Context

The imported timeline playback path can currently prepare, cache, and schedule
one selected imported audio clip. Milestone 3 needs arrangement playback to grow
toward multiple clips, and Milestone 5 will need track volume, pan, mute, solo,
meters, and master mixing.

The project must not move allocation, file I/O, locks, plugin scanning, project
parsing, or UI work onto the audio callback. That means the future render path
needs a prepared snapshot of active voices and mix state, not ad hoc project
queries from the callback.

## Decision

Introduce a plain C++ voice-scheduling boundary for imported timeline audio.

The preparation/session side owns:
- project model reads;
- timeline beat-to-sample planning;
- cache lookup and background decoding for prepared buffers;
- construction of a bounded immutable voice schedule for a render window;
- track mix state snapshots for gain, pan, mute, and solo.

The render side receives:
- prepared sample-buffer pointers that have already been decoded and cached;
- per-voice timeline start, render offset, clip-local start offset, and frame
  count;
- per-voice left/right gain coefficients;
- no project model pointers, filesystem paths, UI references, or plugin scanner
  state.

Mute/solo policy:
- if any track in the snapshot is soloed, only soloed tracks are audible;
- muted tracks are silent unless future product design explicitly introduces
  solo-defeat behavior;
- tracks missing from the mix snapshot default to unity gain, centered pan,
  unmuted, and unsoloed.

Pan policy:
- v0.1 uses linear pan coefficients as a deterministic planning primitive;
- equal-power pan can replace this later behind the same schedule shape if
  listening tests prefer it.

The first testable implementation slice is `TrackVoiceSchedule`: a core planner
that consumes `TimelinePlaybackPlan`, a render-window sample range, and track
mix state, then returns deterministic voice descriptors. It does not mix audio
samples yet; it proves the handoff shape and policy before the audio engine is
expanded.

## Consequences

- Multiple clip/track scheduling can be tested without increasing audio callback
  complexity.
- The callback-facing data shape stays independent of project serialization,
  plugin hosting, and UI controls.
- Gain/pan/mute/solo policy is explicit before deeper mixer UI expands beyond
  the first static strip.
- ADR-0035 adds the first render implementation that consumes these voice
  descriptors and sums prepared buffers into stereo output.
- No new dependency is introduced.

## Follow-Ups

- Expand from first-track static controls to selectable per-track mix strips.
- Keep plugin hosting, plugin delay compensation, and time-stretching in later
  milestones.
