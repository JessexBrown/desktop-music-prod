<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0084: Package Media Maintenance Selection Controls

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0083 added the first read-only Package Maintenance status surface in the
JUCE browser panel. Users now need to inspect more than the default cleanup
batch without exposing mutating cleanup or restore actions prematurely.

The selection behavior must stay testable outside JUCE and must not cause
package filesystem scans, file moves, plugin scans, or audio-thread work.

## Decision

Add a plain C++ `PackageMediaMaintenanceBrowserRows` model that converts
`PackageMediaMaintenanceViewModel` into visible browser rows with row kind,
display text, selectable cleanup id, and selected state.

The JUCE `WorkspacePanel` consumes those rows through generic selectable-row
metadata. Mouse selection chooses a visible cleanup batch row. Keyboard Up/Down
selection moves through the view model's cleanup batch list and clamps at the
newest/oldest batch. Selection updates the current view model immediately with
`selectPackageMediaMaintenanceBatch`, then future background scans reuse the
selected cleanup id and fall back to the newest valid batch if it disappears.

The visible browser surface remains read-only. This decision does not add
cleanup execution, restore execution, permanent deletion, retention policy,
plugin scanning, or file I/O on the audio callback.

## Consequences

- Selected cleanup batches are keyboard and mouse reachable from the browser
  surface.
- The selected batch remains visible even when it is outside the compact
  browser row window.
- Stale selected ids continue to fall back through the existing maintenance
  view-model rules.
- Browser row rendering and selection edge cases can be tested in the plain C++
  suite without launching JUCE.
- No dependency is added.

## Follow-Ups

- Add a guarded restore action affordance for selected restorable batches.
- Add a non-mutating preview/details surface for selected cleanup batch entries.
- Define retention rules before any permanent deletion command.
