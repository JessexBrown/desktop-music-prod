<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0083: Package Media Maintenance Status Surface

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0080 through ADR-0082 added cleanup status mapping, cleanup batch discovery,
and a package media maintenance view model. The app now needs the first visible
Package Maintenance surface, but cleanup and restore execution should remain out
of scope until the status/readiness path is visible and verified.

Inventory and cleanup-batch discovery read the project package filesystem, so
they must not run on the real-time audio callback. They should also avoid
blocking the UI thread.

## Decision

Add a compact read-only Package Media Maintenance summary to the existing JUCE
browser panel.

The browser surface shows:

- package media scan state;
- inventory status and candidate counts;
- cleanup batch count and selected batch status;
- restore enablement state from `PackageMediaMaintenanceViewModel`;
- up to two cleanup batch rows;
- discovery issue count.

The visible surface is backed by an app-level background `std::async` snapshot
that builds package inventory, discovers cleanup batches, and reduces them into
`PackageMediaMaintenanceViewModel`. The UI thread only starts and polls the
snapshot, then paints the completed model. Repeated package changes coalesce by
marking a pending refresh while an existing scan is running.

This decision does not add cleanup execution, restore execution, permanent
deletion, retention policy, plugin scanning, modal maintenance dialogs, or file
I/O on the audio callback.

## Consequences

- Users can see package cleanup readiness and recovery states before any
  mutating controls are exposed.
- The first visible maintenance surface stays non-modal and aligned with the
  browser-first workflow.
- Package filesystem scanning stays off the UI and audio threads.
- Restore and cleanup commands remain future explicit actions.
- No dependency is added.

## Follow-Ups

- Add guarded restore controls for selected cleanup batches.
- Add restore execution through the existing background cleanup job.
- Define retention rules before any permanent deletion command.
