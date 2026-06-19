<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0093: Background Save As Package Copy Job

## Status

Accepted.

## Context

ADR-0092 made Save As package relocation safe by copying package-local media
before writing the target manifest. That command was correct but still ran from
the native chooser completion path, so large packages could make the UI feel
stuck while file I/O was running.

Package copying remains non-real-time work. It must not run on the audio
callback, and cancellation should preserve the copy command's rollback boundary.

## Decision

Add `BackgroundSaveAsPackageCopyJob` to `projectname_core`.

The job:

- owns a copy of the project snapshot, source package path, and target package
  path chosen by Save As;
- runs `copyProjectPackageAssetsForSaveAs` through `std::async`;
- exposes atomic phase, percent, file count, and byte count progress;
- forwards cancellation to the copy command;
- lets the copy command roll back created target files/directories on failure
  or cancellation;
- reports completion to the JUCE app so the UI thread can save the target
  manifest and switch the active package path afterward.

The copy command now accepts optional cancellation and progress callbacks. It
uses chunked file copying so cancellation can stop a large file copy between
chunks and progress can report bytes copied.

## Consequences

- Save As package asset copying no longer blocks the native file chooser
  completion callback.
- The top bar exposes a guarded `Cancel Save` affordance while Save As package
  copy work is running.
- Package media cleanup, import, relink, and project chooser actions treat Save
  As copy work as active package file work.
- Manifest saving still happens after the background copy completes; that final
  manifest write remains short and uses the existing package save path.

## Follow-Ups

- Add deterministic chooser-level smoke coverage for New/Open/Save As once the
  app has a test hook for native chooser results.
- Consider a future manifest-save background wrapper if manifest size becomes
  large enough to make final Save As commits visible to users.
