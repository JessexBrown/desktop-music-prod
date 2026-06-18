<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0082: Package Media Maintenance View Model

## Status

Accepted for the v0.1 prototype.

## Context

Package media maintenance now has separate models for inventory, cleanup status,
and cleanup batch discovery. The future non-modal Package Maintenance view needs
a single plain C++ model that combines those pieces before visible JUCE controls
are added.

## Decision

Add `PackageMediaMaintenanceViewModel` to `projectname_core`.

The view model reduces already-built inventory and batch discovery results into
UI-ready state:

- inventory status and cleanup candidate counts;
- discovered cleanup batch rows and discovery issues;
- selected cleanup batch fallback;
- selected-row marking;
- restore action enablement and disabled reason.

Restore is enabled only for a selected cleanup batch with restorable entries and
no recorded restore conflicts or partial-failure entry errors. Conflict and
partial-failure batches remain selectable and reviewable, but the first visible
UI must not offer a repeat restore action until conflict/partial-failure recovery
behavior is designed.

This view model does not scan the filesystem, move files, delete quarantine
contents, define retention policy, create JUCE UI, scan plugins, or run on the
real-time audio callback.

## Consequences

- Future Package Maintenance UI can bind to stable rows and action state without
  duplicating cleanup policy.
- Corrupt or suspicious cleanup batch discovery issues stay visible alongside
  valid batches.
- Restore controls start conservative around conflict and partial-failure states.
- No dependency is added.

## Follow-Ups

- Wire the view model into a visible non-modal browser/status Package
  Maintenance surface.
- Add recovery actions for restore conflicts and partial-failure batches.
- Define retention rules before any permanent deletion command.
