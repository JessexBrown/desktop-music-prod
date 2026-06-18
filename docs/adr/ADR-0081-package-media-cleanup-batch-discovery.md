<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0081: Package Media Cleanup Batch Discovery

## Status

Accepted for the v0.1 prototype.

## Context

Package-local cleanup moves media into
`backups/media-trash/<cleanup-id>/` and writes one
`restore-manifest.json` per cleanup batch. The status model can describe a
loaded restore manifest, but the future Package Maintenance UI still needs a
safe way to find available restore batches inside a project package.

## Decision

Add `PackageMediaCleanupBatchDiscovery` to `projectname_core`.

The discovery helper scans direct children of `backups/media-trash/`, validates
each cleanup directory name with the restore-manifest cleanup-id validator,
constructs the expected package-relative
`backups/media-trash/<cleanup-id>/restore-manifest.json` path, validates that
path before loading, and then loads the manifest through the existing
restore-manifest parser.

Loaded batches include their manifest, package-relative manifest path,
absolute manifest path, and `PackageMediaCleanupStatus`. Invalid cleanup ids,
missing manifests, unreadable or invalid manifests, cleanup-id mismatches, and
scan failures become non-fatal discovery issues so one bad batch does not hide
other recoverable batches. Loaded batches are sorted newest-first by manifest
creation time and cleanup id.

This helper does not move files, delete quarantine contents, define retention
policy, create JUCE UI, scan plugins, or run on the real-time audio callback.

## Consequences

- Future Package Maintenance UI can show existing cleanup batches without
  duplicating path and manifest validation.
- Restore-conflict and partial-failure batches remain visible for recovery.
- Corrupt or suspicious cleanup folders are surfaced as reviewable issues
  instead of causing the whole restore list to fail.
- No dependency is added.

## Follow-Ups

- Add a package media maintenance view model that combines inventory status,
  batch discovery, selected batch state, and restore action enablement.
- Wire the view model into a visible non-modal browser/status surface.
- Define retention rules before any permanent deletion command.
