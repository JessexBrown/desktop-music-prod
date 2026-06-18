<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0022: Imported Audio Clip Placement

## Status

Accepted for the v0.1 prototype.

## Context

Milestone 3 requires imported audio clips to be placed on a timeline and saved
with the project. ADR-0021 made imported clips visible in a beat-scaled lane,
but import behavior still needed an explicit placement boundary. Placement must
not decode audio, touch the realtime audio callback, or disturb the prepared
buffer handoff used for immediate playback after import.

## Decision

Add `ProjectModel::setImportedAudioClipStartBeats` and a matching `AppSession`
command for imported audio clip placement. The command:

- finds clips by id across project tracks;
- accepts only `audio-file` clips;
- validates finite non-negative start beats;
- mutates only the in-memory manifest model;
- leaves saving as an explicit project/package operation.

Extend project audio import options with `requestedStartBeats`.

When no explicit start beat is requested, import chooses the next deterministic
non-overlapping start beat on the target track by scanning existing finite clip
start/length ranges. In the default project this places the first imported audio
clip after the starter generated clip. When an explicit start beat is requested,
import uses it after validation and rejects invalid values before package
mutation.

Background import requests forward the optional requested start beat into the
same commit path. Prepared mono audio samples returned by import are unchanged
by placement; placement affects only `ProjectClip::startBeats`.

## Consequences

- Imported audio clips now have a project/session placement command that can be
  called by future drag/drop or inspector UI.
- Project save/load persists moved `startBeats` through the existing manifest
  model.
- Repeated imports no longer stack at beat zero by default.
- The JUCE workspace and Win32 fallback refresh their shared lane layout after
  import, so newly placed clips appear in timeline order.
- No new dependency is introduced.

## Follow-Ups

- Add interactive drag/drop or inspector controls for clip placement.
- Add true per-track lane editing once track add/remove/reorder commands exist.
- Loop-region state is recorded in ADR-0023; add transport playback behavior
  next.
