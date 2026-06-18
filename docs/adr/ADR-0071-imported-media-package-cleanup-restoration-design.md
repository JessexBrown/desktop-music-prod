<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0071: Imported Media Package Cleanup and Restoration Design

## Status

Accepted for the v0.1 prototype.

## Context

Imported audio now enters a project package through staged writes. Audio import
copies PCM16 WAV files under `audio/`, writes waveform summaries under
`analysis/`, updates `manifest.json`, and keeps `backups/manifest.previous.json`
as the last-good manifest. Imported clip media relink follows the same package
shape, but preparation can happen on a background job under
`.projectname-staging/` before the UI commits the replacement through
`AppSession`.

Media replacement undo/redo is intentionally manifest-only. The old and new
`audio/...` and `analysis/...` references stay in the package so live session
history can move between them without copying or restoring files. That means a
future cleanup feature must know more than "not in the current manifest" before
it can safely remove anything.

## Decision

Treat imported media cleanup as a staged package maintenance workflow:

1. Build a read-only inventory first.
2. Classify package-owned media and staging paths.
3. Present or test a dry-run result.
4. Add any destructive move or deletion behavior in a later, separately
   reviewed task.

The cleanup inventory must classify a package asset as referenced when any of
these sources mention it:

- the current `manifest.json`;
- `backups/manifest.previous.json`, until a separate backup-retention decision
  narrows or removes that protection;
- live `AppSession` imported clip media replacement undo/redo history, when the
  inventory runs inside an active session;
- in-flight or completed-but-uncommitted background audio import and media
  relink jobs;
- staged import or relink completion objects that are still owned by the UI;
- any temporary manifest path involved in an active save.

An asset under `audio/` or `analysis/` is only an unreferenced candidate when it
is package-relative, normalized, inside the package root, and absent from every
protected reference set above. Staging directories under `.projectname-staging/`
are only stale candidates when no import/relink job is running and no pending
UI completion can still commit or discard that staging directory.

Cleanup must not run destructive work during save, open, undo, redo, import,
relink, waveform regeneration, or timeline playback preparation. Inventory may
eventually run as a background read-only job, but any future file move/delete
phase must be explicit, off the audio thread, path-checked, and serialized with
package writers. The audio callback must never inspect, delete, log, allocate
for, or coordinate cleanup.

Restoration is part of the cleanup contract:

- media replacement undo/redo must keep working while live session history
  references older media;
- loading a project clears live edit history, but the previous-manifest backup
  still protects paths from the last save;
- future destructive cleanup must move files to a recoverable package-local
  quarantine, such as `backups/media-trash/<cleanup-id>/`, with a
  human-readable restore manifest before any permanent deletion exists;
- if a user later requests restoration for media that is genuinely missing, the
  project should preserve the clip and surface a missing-media state instead of
  corrupting the manifest.

This decision does not add deletion UI, command-palette entries, restoration UI,
drag/drop clip operations, or permanent package retention rules.

## Consequences

- The next implementation slice can be a small tested inventory/dry-run model
  instead of a destructive cleanup command.
- Existing undo/redo and previous-manifest behavior remain protected.
- Background import/relink staging is treated as job-owned until proven stale.
- Future deletion work has a restoration requirement before it can remove
  package assets.
- No dependency is added.

## Follow-Ups

- Add package media quarantine restore-manifest model tests.
- Define backup-manifest retention before any permanent media deletion task.
