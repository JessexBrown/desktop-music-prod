<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0086: Package Media Maintenance Cleanup Guard

## Status

Accepted for the v0.1 prototype.

## Context

The Package Maintenance browser surface can scan project media, select cleanup
batches, and restore selected restorable batches. Users also need a first
cleanup/quarantine affordance for current unused package media, but permanent
deletion and retention policy are still out of scope.

Cleanup is a package file operation. It must not run from the UI thread or the
real-time audio callback.

## Decision

Add a guarded Clean affordance beside Restore in the Package Maintenance browser
surface.

The plain C++ browser-row model now exposes cleanup action state:

- visible after a package maintenance snapshot exists;
- enabled only when `PackageMediaMaintenanceViewModel::cleanupReviewAvailable`
  is true and no package file work is active;
- disabled with a visible reason for empty candidates, missing references,
  unsafe references, scan-waiting, and active package work.

When activated, the JUCE app creates a filesystem-safe cleanup id, then starts
the existing `BackgroundPackageMediaCleanupJob` with the `quarantine` operation.
The app polls progress on the timer, refreshes the package maintenance snapshot
when the job completes, and selects the newly created cleanup batch when a
restore manifest was written.

This decision does not add permanent deletion, retention policy, plugin
scanning, modal cleanup dialogs, or audio-callback filesystem work.

## Consequences

- Users can move current cleanup candidates to Project Media Trash from the
  visible Package Maintenance surface.
- Cleanup disabled reasons are testable without launching JUCE.
- Cleanup and restore share the same background package job boundary.
- No dependency is added.

## Follow-Ups

- Add a selected-batch detail surface showing moved/restored/conflict entries.
- Add confirmation/review details before expanding cleanup scope.
- Define retention rules before any permanent deletion command.
