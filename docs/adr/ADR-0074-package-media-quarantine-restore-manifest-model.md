<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0074: Package Media Quarantine Restore Manifest Model

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0073 defined the package-local media quarantine and restore command
contract. Before adding a cleanup command that moves files, the project needs a
plain C++ restore-manifest model that can serialize, load, and validate the JSON
contract independently.

The model must support partial transactions and restore conflicts without
performing any package file moves.

## Decision

Add `PackageMediaQuarantineRestoreManifest` to `projectname_core`.

The model records:

- schema version;
- app name;
- cleanup id;
- creation time;
- display-only package path;
- inventory summary;
- optional manifest/save marker;
- manifest state;
- manifest-level error;
- moved entries;
- skipped entries.

Moved entries include original package-relative path, quarantine
package-relative path, entry kind, optional byte size, optional content hash,
restore flags, conflict flag, and per-entry error. Skipped entries include
original package-relative path, entry kind, reason, and optional detail.

Validation requires:

- schema version 1;
- non-empty app name, cleanup id, creation time, and inventory summary;
- filesystem-safe cleanup ids with no path separators;
- safe normalized package-relative paths;
- original paths under `audio/`, `analysis/`, or `.projectname-staging/`
  according to entry kind;
- quarantine paths under `backups/media-trash/<cleanup-id>/audio`,
  `backups/media-trash/<cleanup-id>/analysis`, or
  `backups/media-trash/<cleanup-id>/staging`;
- no duplicate original paths or quarantine paths among moved entries;
- partial-failure state carries a manifest-level error;
- restore-conflict state carries at least one conflicting moved entry.

The model can save and load human-readable JSON, but it does not create
quarantine ids, choose candidates, move files, rollback files, restore files,
delete files, touch UI state, or run on the audio callback.

## Consequences

- The restore manifest format now has tests before file-moving commands exist.
- Unsafe and conflicting manifest fixtures fail at the model boundary.
- Partial transaction error state can be represented and loaded.
- Future quarantine preflight can generate manifests without duplicating JSON
  validation rules.
- No dependency is added.

## Follow-Ups

- Add package media quarantine preflight plan model.
- Define backup-manifest retention before any permanent media deletion task.
- Design user-facing package maintenance UI after quarantine and restore
  commands are implemented and tested.
