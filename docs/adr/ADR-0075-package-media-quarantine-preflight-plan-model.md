<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0075: Package Media Quarantine Preflight Plan Model

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0073 defines package-local media quarantine and restore command behavior.
ADR-0074 added a restore-manifest model that can serialize, load, and validate
quarantine metadata. Before the project adds a command that moves files, it
needs a dry-run preflight layer that turns inventory results into a
restore-manifest draft and rejects unsafe cleanup states.

## Decision

Add `PackageMediaQuarantinePreflightPlan` to `projectname_core`.

The preflight request carries:

- imported media package inventory;
- cleanup id;
- creation time;
- display-only package path;
- optional manifest marker;
- optional explicitly requested package-relative paths;
- package-work-in-progress flag.

The preflight builder rejects:

- invalid cleanup ids;
- package work in progress;
- active staging directories;
- inventory errors;
- unsafe inventory references;
- missing inventory references;
- explicitly requested protected package assets;
- duplicate requested paths that would produce duplicate quarantine
  destinations;
- empty plans with no movable candidates.

When valid, the preflight builder returns a `PackageMediaQuarantineRestoreManifest`
draft. Unreferenced `audio/` assets map to
`backups/media-trash/<cleanup-id>/audio/...`; unreferenced `analysis/` assets
map to `backups/media-trash/<cleanup-id>/analysis/...`; stale staging
directories map to `backups/media-trash/<cleanup-id>/staging/...`.

The draft includes protected inventory assets as skipped entries. It may record
byte size for regular audio/analysis assets when available. It does not compute
content hashes, create directories, write manifests, move files, rollback files,
restore files, delete files, touch UI state, or run on the audio callback.

## Consequences

- Future quarantine commands can start from a tested manifest draft instead of
  rebuilding package-path mapping logic.
- Cleanup stays blocked until unsafe, missing, protected, and active-work states
  are resolved.
- Stale staging cleanup is planned through the same restore-manifest contract
  as imported audio and analysis assets.
- No dependency is added.

## Follow-Ups

- Add package media quarantine file-moving command.
- Define backup-manifest retention before any permanent media deletion task.
- Design user-facing package maintenance UI after quarantine and restore
  commands are implemented and tested.
