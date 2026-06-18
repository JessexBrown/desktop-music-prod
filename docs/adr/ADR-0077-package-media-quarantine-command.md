<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0077: Package Media Quarantine Command

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0073 defined the package-local media quarantine transaction. ADR-0074 added
the restore-manifest model, and ADR-0075 added a preflight planner that creates
validated restore-manifest drafts without moving files.

The next incremental step is the plain C++ package-writer command that moves
the planned files and stale staging directories into quarantine while preserving
rollback and restore metadata.

## Decision

Add `PackageMediaQuarantineCommand` to `projectname_core`.

The command accepts:
- a project package directory;
- a completed `PackageMediaQuarantineRestoreManifest` draft from preflight.

The command:
- validates the draft before touching files;
- writes `backups/media-trash/<cleanup-id>/restore-manifest.json.tmp`;
- moves each planned audio file, analysis file, or stale staging directory to
  its draft quarantine path;
- commits the temporary manifest to `restore-manifest.json` after all moves
  succeed;
- removes the temporary manifest when a move fails and rollback succeeds;
- writes a partial-failure restore manifest when rollback cannot fully restore
  already-moved entries.

The command reports occupied quarantine destinations, missing sources, move
failures, manifest write failures, commit failures, and rollback failures as
explicit statuses. It does not create cleanup ids, build inventories, select
candidates, restore quarantined media, permanently delete files, touch UI state,
scan plugins, or run on the real-time audio callback.

## Consequences

- Package media cleanup now has a reversible file-moving primitive.
- User-facing cleanup UI can be added later without duplicating package move
  transaction rules.
- Restore remains a separate future command so this slice stays reviewable.
- No dependency is added.

## Follow-Ups

- Add package media quarantine restore command.
- Add background-job/UI wiring for package media cleanup after restore exists.
- Define retention rules before any permanent deletion command.
