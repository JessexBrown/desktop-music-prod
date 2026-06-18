<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0038: Persisted Track Mix State

## Status

Accepted for the v0.1 prototype.

## Context

The voice-window scheduler already accepts `TrackMixState` snapshots with gain,
pan, mute, and solo values. Until now those snapshots existed only in tests and
could not be saved in a project manifest. That meant app-session timeline
playback always built schedules with default unity gain, center pan, and no
mute/solo filtering.

The next small step is model-first: persist track mix state and feed it into
prepared voice-window scheduling. Mixer UI, automation, plugin routing, and
device-chain processing remain later tasks.

## Decision

Add mix fields directly to `ProjectTrack`:
- `volume`, default `1.0`;
- `pan`, default `0.0`;
- `muted`, default `false`;
- `solo`, default `false`.

The manifest writes those fields on each track. Loading older manifests without
the fields keeps the same safe defaults. `AppSession` converts current project
tracks into `TrackMixState` snapshots before calling `buildTrackVoiceSchedule`,
so prepared timeline voice windows now honor persisted track volume, pan, mute,
and solo.

## Consequences

- Project save/load can round-trip basic track mix state.
- App-session prepared voice-window playback applies persisted pan and volume.
- Muted tracks are omitted from prepared voice schedules, and soloed tracks mute
  non-soloed tracks before rendering begins.
- The audio callback still only consumes immutable prepared buffers and a
  prebuilt schedule; it does not read project state.
- No new dependency is introduced.

## Follow-Ups

- Implement resampling before claiming production-quality playback for files
  whose sample rates differ from the output device, following ADR-0040.
- Add automation lanes after static mix state is stable.
