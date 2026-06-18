<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0023: Timeline Loop Region State

## Status

Accepted for the v0.1 prototype.

## Context

Milestone 3 calls for a loop region as part of timeline audio clip playback.
Before applying loop behavior to realtime playback or audio scheduling, the
project needs a validated and persisted loop-region model that survives
save/load and remains compatible with older manifests.

Loop state is project metadata. Setting or clearing it must not require audio
decoding, audio callback work, or direct engine mutation.

## Decision

Add `ProjectLoopRegion` to `ProjectModel`.

The project manifest now writes:

- `loopRegion.enabled`;
- `loopRegion.startBeats`;
- `loopRegion.lengthBeats`.

The loader keeps older manifests loadable by treating a missing `loopRegion`
object as disabled. Enabled loop regions must have a finite non-negative start
beat, a finite positive length in beats, and a finite end beat. Invalid enabled
loop regions reject the manifest with a descriptive error. Disabled loop regions
reset to zero-valued beats.

Add project and session commands:

- `setLoopRegion(startBeats, lengthBeats, error)`;
- `clearLoopRegion()`;
- `getLoopRegion()`.

These commands mutate only the project/session model. They do not touch the
audio callback, prepared clip buffers, or audio device state.

## Consequences

- Loop state round trips through the human-readable manifest.
- Older v1 manifests without loop state still load with loop disabled.
- The session can expose loop settings to future UI controls without coupling
  them to audio rendering.
- Session-domain transport advancement wraps at loop boundaries as recorded in
  ADR-0024.
- No new dependency is introduced.

## Follow-Ups

- Render loop range affordances in the timeline lane.
- Add loop-region editing controls after the project chooser work reduces the
  deterministic demo-project coupling.
