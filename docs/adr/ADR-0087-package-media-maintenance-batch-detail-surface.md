<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0087: Package Media Maintenance Batch Detail Surface

## Status

Accepted.

## Context

The Package Maintenance browser can list cleanup batches, select one, and run
guarded cleanup or restore operations through background jobs. Users now need
more review detail for the selected batch before taking action, especially when
a restore has already completed, hit conflicts, or recorded partial failures.

The detail surface must remain non-mutating. It should not introduce modal
dialogs, permanent deletion, retention policy, or new package file operations.

## Decision

Extend `PackageMediaMaintenanceViewModel` with lightweight selected-batch entry
previews copied from the loaded restore manifest:

- original package-relative path;
- quarantine package-relative path;
- restored flag;
- restore-conflict flag;
- per-entry error presence.

Extend the plain C++ browser-row model with selected-batch detail rows:

- moved, restored, restorable entry counts;
- conflict and error counts;
- up to two package-relative entry path previews by default, with status labels.

The JUCE browser panel consumes these rows through its existing adapter, so no
new UI ownership boundary or modal workflow is introduced.

## Consequences

- Completed, restored, conflict, partial-failure, and no-selection states are
  visible and testable in the core row model.
- Users get enough package-relative path context to review a selected cleanup
  batch without opening the restore manifest manually.
- Cleanup execution, restore execution, permanent deletion, retention policy,
  and conflict recovery actions remain separate future tasks.

## Follow-Ups

- Add an entry-selection model for restoring a subset of restorable paths.
- Add conflict and partial-failure recovery affordances after the recovery
  policy is documented.
