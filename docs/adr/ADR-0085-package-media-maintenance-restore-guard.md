<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0085: Package Media Maintenance Restore Guard

## Status

Accepted for the v0.1 prototype.

## Context

The Package Maintenance browser surface can list and select cleanup batches.
Restoring a cleanup batch is a package file operation, so the UI must expose it
only when the selected batch is eligible and must run it away from the UI and
audio threads.

## Decision

Add a guarded Restore affordance to the Package Maintenance browser surface.

The plain C++ browser-row model now exposes restore action state:

- visible after a package maintenance snapshot exists;
- enabled only when `PackageMediaMaintenanceViewModel::restoreActionEnabled`
  is true;
- disabled with the view model's unavailable reason for no-selection,
  already-restored, conflict, partial-failure, and other non-restorable states.

The selected batch row now carries its package-local restore manifest path from
cleanup batch discovery. When the user activates Restore, the JUCE app creates a
`BackgroundPackageMediaCleanupJob` with the `restore` operation and the selected
manifest path. The app polls progress on the timer, refreshes the package
maintenance snapshot when the job completes, and does not move package files
directly from the UI thread.

Restore is disabled while scans or other package file jobs are active. This
decision does not add permanent deletion, retention policy, plugin scanning, or
audio-callback filesystem work.

## Consequences

- Users can recover a restorable cleanup batch from the visible Package
  Maintenance surface.
- Disabled restore reasons remain visible/testable before the user acts.
- Restore execution reuses the existing cancellable background cleanup job.
- No dependency is added.

## Follow-Ups

- Add a guarded cleanup/quarantine affordance for cleanup candidates.
- Add a selected-batch detail surface showing moved/restored/conflict entries.
- Define retention rules before any permanent deletion command.
