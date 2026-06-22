<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0092: Save As Package Asset Copy Command

## Status

Accepted.

## Context

ADR-0091 defined the Save As package relocation policy, but package-local media
still needed a concrete writer before Save As could safely move active projects
that reference `audio/...` or `analysis/...` assets.

Package copying is file I/O and must never run on the real-time audio callback.
It also needs a narrow rollback boundary so partial target writes do not leave a
new project package in a misleading state when a copy fails.

## Decision

Add a plain C++ `copyProjectPackageAssetsForSaveAs` command in the Save As
policy module. The command:

- builds the ADR-0091 preflight plan for the source and target packages;
- returns `noCopyNeeded` for same-package saves or projects without package-local
  media to copy;
- rejects target packages nested inside the source package;
- recursively copies package-local `audio/`, `analysis/`, `samples/`, and
  `presets/` contents when those folders exist in the source package;
- does not copy source-package `backups/`;
- preserves explicit external references by leaving them in the manifest rather
  than copying external files;
- rejects symlinked, non-regular, or non-directory source entries;
- preflights target conflicts before mutating the target package;
- tracks created files and directories and removes them on copy failure when
  rollback is possible.

The native `Save As...` handler runs this command before calling the existing
manifest save path and switching the active package path. If the copy command
completes but the final manifest save fails, the active project package remains
unchanged and copied assets stay in the selected target package so the user can
inspect, recover, retry, or clean them up explicitly.

## Consequences

- Save As can now relocate projects with package-local imported media without
  producing a manifest that points at missing target-package files.
- Target backup history starts fresh because source `backups/` are never copied.
- Conflict failures happen before partial target copies.
- Final manifest-save failures after a completed copy do not roll back copied
  target assets yet; they leave a recoverable target package candidate while the
  active project keeps using the source package.
- ADR-0093 wraps the copy command in a background job with progress and
  cancellation for the JUCE app.

## Follow-Ups

- Add deterministic chooser-level smoke coverage for New/Open/Save As once the
  app has a test hook for native chooser results.
