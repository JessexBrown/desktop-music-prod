<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0039: Static Track Mix Controls

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0038 persisted static track volume, pan, mute, and solo state and fed it
into prepared voice-window scheduling. The native app still showed only mixer
placeholder text, so users could not edit those values from the shell.

The first UI slice should stay small and respect the realtime boundary: UI
callbacks must update project/session state only, while audio rendering
continues to consume prepared schedules and immutable buffers installed before
playback.

## Decision

Add a compact static mixer strip to the existing bottom mixer area for the first
audio track:
- volume slider;
- pan slider;
- mute toggle;
- solo toggle;
- track name label.

The JUCE component reads the current first audio track to refresh control state,
but writes through `AppSession::setTrackMixState`. The session command validates
track id, volume, and pan before mutating the project model. Save, Open, and
Import refresh the mixer controls from the current project snapshot.

These controls edit static project state. They do not push live parameter
changes into an already-running audio callback or rebuild the active playback
schedule.

## Consequences

- Users can edit the first audio track's static volume, pan, mute, and solo from
  the app shell.
- Project mutation stays behind a tested app-session command.
- The realtime callback still does not read UI or project state.
- This is not a full mixer: there are no meters, multi-track strips, automation,
  or live schedule updates yet.

## Follow-Ups

- Add per-track mixer strips once track creation/selection exists.
- Implement resampling before claiming production-quality playback for files
  whose sample rates differ from the output device, following ADR-0040.
- Add metering and automation after static mix edits are stable.
