<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0042: Deterministic Imported Clip Selection

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0041 added a read-only inspector for imported audio metadata, but it always
used the first imported audio clip in project track order. That was useful as a
placeholder, but the DAW needs a stable selection boundary before adding timeline
mouse hit-testing, inspector edits, or undoable clip operations.

Selection must remain project/session state, not audio callback state. A stale
selection must not make project load fail or hide imported media from the user.

## Decision

Add `selection.clipId` to the project manifest and `ProjectModel` as the
deterministic selected clip id. `ProjectModel::selectImportedAudioClip` and
`AppSession::selectImportedAudioClip` only accept existing `audio-file` clips.
Generated clips and missing ids are rejected without changing the prior
selection. Clearing selection is explicit.

Project load preserves the stored selected clip id even if it is stale. The
imported-clip inspector resolves the selected id first and falls back to the
first imported audio clip when the selected id is empty, missing, or no longer an
imported audio clip. This keeps reopened projects resilient while preserving
enough state for future selection UI.

`AppSession::importPcm16WavIntoProjectPackage` and the JUCE background import
completion path select the newly imported clip so the inspector follows the most
recent import.

## Consequences

- Save/load can round-trip imported clip selection.
- The inspector can follow deterministic selection from import commands and,
  after ADR-0043, timeline mouse hit-testing.
- Stale selection remains recoverable and falls back to visible imported media.
- No audio callback behavior changes.
- No new dependency is introduced.

## Follow-Ups

- Design package-local media quarantine and restore commands.
