<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0079: Background Package Media Cleanup Job

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0072 through ADR-0078 added imported media inventory, quarantine preflight,
quarantine file moves, and restore commands. Those commands can touch the
project package filesystem and must not run on the real-time audio callback or
block the UI thread once they are exposed through the app.

The next step is a background wrapper that can drive the existing plain C++
commands while reporting progress and preserving cancellation handoff points.

## Decision

Add `BackgroundPackageMediaCleanupJob` to `projectname_core`.

The job supports two operations:
- quarantine: build inventory, build preflight, and move candidates to
  quarantine;
- restore: load a quarantine restore manifest and restore selected or all
  moved entries.

The job reports phases for pending, inventory, preflight, quarantining,
restoring, completed, failed, and cancelled. It stores progress in atomics and
runs work through `std::async`, matching the current background import/relink
job pattern. Cancellation is honored before start and at command handoff
points; long-running file move interruption remains a future refinement once a
chunked move primitive exists.

The job does not create visible UI, delete media permanently, define retention
policy, scan plugins, mutate audio-thread state, or bypass the validated
preflight/restore-manifest command boundaries.

## Consequences

- Future package maintenance UI can run cleanup and restore without blocking
  the UI thread.
- Tests can assert status propagation for successful quarantine, successful
  restore, active package work, cancellation before mutation, and command
  failures.
- File moves remain isolated to the existing package writer commands.
- No dependency is added.

## Follow-Ups

- Design package media cleanup and restore UI states.
- Add visible package maintenance controls after the UX flow is documented.
- Define retention rules before any permanent deletion command.
