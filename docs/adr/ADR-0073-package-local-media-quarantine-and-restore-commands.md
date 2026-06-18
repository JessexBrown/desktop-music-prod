<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0073: Package-Local Media Quarantine and Restore Commands

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0071 requires recoverable package-local cleanup before any permanent media
deletion exists. ADR-0072 added a read-only imported media package inventory
that can identify unreferenced audio files, waveform summaries, missing
references, unsafe references, and staging directories.

The project now needs a command design for moving inventory candidates out of
active package folders while preserving enough information to restore them
later. The first step is a format and transaction policy, not a visible cleanup
command or file-moving implementation.

## Decision

Use a package-local quarantine rooted under:

```text
backups/media-trash/<cleanup-id>/
  restore-manifest.json
  audio/...
  analysis/...
  staging/...
```

`<cleanup-id>` must be unique, filesystem-safe, and sortable by creation time.
The first implementation may use an ISO-like timestamp plus a short random
suffix. It must not contain path separators.

`restore-manifest.json` is human-readable JSON. It must include:

- a schema version;
- the app name and cleanup id;
- cleanup creation time;
- the project package path only as display text, never as an authority for path
  traversal;
- the inventory snapshot summary used to choose candidates;
- one entry per moved asset with original package-relative path, quarantine
  package-relative path, media kind, byte size when available, and optional
  content hash when a future job computes one off the audio thread;
- one entry per moved staging directory with original and quarantine
  package-relative paths;
- the active manifest generation or save marker when available;
- warnings for any skipped unsafe, missing, or actively protected inventory
  item.

Move-to-quarantine must be a package-writer operation serialized with save,
open, import, relink, waveform regeneration, timeline playback preparation, and
future cleanup/restore jobs. It must never run from the audio callback. It may
only move candidates from ADR-0072 inventory results that are still unreferenced
after a fresh preflight inventory. Current manifest references, previous
manifest backup references, session-protected undo/redo references, active
staging, missing references, and unsafe references are not movable.

The transaction order is:

1. Build a fresh inventory and select movable candidates.
2. Create a new quarantine directory and write `restore-manifest.json.tmp`.
3. Move files/directories to quarantine paths that preserve their original
   folder grouping.
4. Rewrite the restore manifest with final move results.
5. Rename `restore-manifest.json.tmp` to `restore-manifest.json`.

If any move fails, rollback must move already-quarantined items back to their
original package-relative paths when those paths are still empty. If rollback
cannot fully restore state, the command must leave the partial quarantine
manifest in a recoverable error state and report a human-readable error. It must
not remove partially moved media to hide the failure.

Restore is also a package-writer operation. It reads one quarantine restore
manifest, verifies that quarantine paths still live inside the package, and
moves selected items back only when the original package-relative path is empty.
If the original path is now occupied, restore must skip that item and report a
conflict rather than overwriting active project media. Restoring files does not
automatically rewrite `manifest.json`; current clips that reference missing
media should become usable again when their package-relative files are restored.
Future missing-media placeholders can use restore results to refresh status, but
this ADR does not define placeholder UI.

Previous manifest backups keep protecting referenced media until a separate
backup-retention ADR says otherwise. Permanent deletion of quarantine contents
is out of scope and must not be added before retention, confirmation, and
recovery behavior are designed.

This decision does not add a cleanup implementation, restore implementation,
visible UI, command-palette entries, drag/drop operations, permanent deletion,
or backup-retention policy.

## Consequences

- Cleanup can become reversible by construction instead of relying on platform
  trash behavior.
- The restore manifest provides a testable contract before any file-moving
  command exists.
- Active manifests, previous backups, live undo/redo history, and in-flight
  package jobs remain protected.
- Future restore commands can recover media without mutating the project
  manifest.
- No dependency is added.

## Follow-Ups

- Add package media quarantine preflight plan model.
- Define backup-manifest retention before any permanent media deletion task.
- Design user-facing package maintenance UI after quarantine and restore
  commands are implemented and tested.
