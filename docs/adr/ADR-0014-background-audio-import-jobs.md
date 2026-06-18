<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0014: Background Audio Import Jobs

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0013 added a native Import button, but project-package import still ran on
the UI thread after file selection. WAV decoding, file copying, and manifest
writes are allowed outside the realtime audio callback, but they should not block
the UI as file sizes grow.

The project also needs automated coverage for import success, failure, and
cancellation boundaries before expanding the user-facing import workflow.

## Decision

Add `BackgroundAudioImportJob` to `projectname_core`.

The job:

- owns a copy of the current `ProjectModel`;
- runs `ProjectAudioImport` through `std::async` with `std::launch::async`;
- returns a completed project, import result, error string, and cancellation
  flag;
- preserves the live session until the UI explicitly applies a successful
  result;
- supports pre-start cancellation before any decode/copy/manifest work begins.

The JUCE app starts the background job after native file selection, displays an
importing status, polls completion from its timer, then applies the completed
project and prepared-buffer playback handoff on the message thread.

The Win32 fallback import smoke uses the same background job so local MinGW
verification covers the asynchronous core path.

## Consequences

- File I/O, decode, allocation, and manifest writes are now off the UI path used
  after file selection.
- The audio render callback remains free of allocation, locks, file I/O, project
  parsing/writing, and UI calls.
- Tests cover background import success, failure without project mutation, and
  cancellation before the job starts.
- ADR-0015 and ADR-0016 build on this job with copy-byte progress, staged
  package writes, and cancellation during staged copy.

## Follow-Ups

- Keep background-job progress aligned with ADR-0015, ADR-0016, and ADR-0017.
- Add a shared background-job service for future waveform, analysis, and plugin
  scan tasks.
