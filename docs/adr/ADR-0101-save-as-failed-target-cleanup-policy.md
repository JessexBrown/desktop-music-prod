<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0101: Save As Failed Target Cleanup Policy

## Status

Accepted.

## Context

ADR-0092 and ADR-0093 made `Save As...` relocate package-local media by copying
target package assets before writing the final target `manifest.json`. A final
manifest commit can still fail after the copy succeeds, for example if the
target path is occupied by an unexpected directory or the file system rejects
the commit.

That failure happens after target package media already exists. Automatically
deleting those copied files could remove useful recovery data or surprise a
user who chose that target package. Automatically quarantining them would add a
new package-maintenance state before the app has a specific recovery workflow
for failed Save As targets.

## Decision

When `Save As...` copies package-local assets successfully but the final target
manifest write fails:

- keep the active project package unchanged;
- leave the active source manifest unchanged;
- remove any `manifest.json.tmp` temporary target manifest through the existing
  staged manifest writer;
- keep copied target assets in the selected target package;
- do not quarantine or permanently delete those target assets automatically;
- surface status copy that says copied assets were kept for retry or manual
  cleanup and that the active project stayed on the source package.

The user-facing status copy is shown only for the post-copy manifest failure
case. Preflight failures, copy failures, and cancellations keep their existing
rollback/cancel behavior.

## Consequences

- The app avoids surprising destructive cleanup after a late Save As failure.
- The selected target package remains a recoverable candidate for retry or
  manual cleanup.
- Existing package-copy failure rollback stays focused on failures before a
  completed copy.
- A future recovery UI can reveal the kept target package, retry the manifest
  save, or offer explicit deletion with confirmation.

## Follow-Ups

- Add a non-destructive way to reveal or copy the kept target package path after
  a post-copy Save As manifest failure.
