<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0095: Package Maintenance Restore Keyboard Affordances

## Status

Accepted.

## Context

The Package Maintenance browser can select cleanup batches and restore selected
package media entries, but restore-entry selection still depended on pointer
input for individual rows. Keyboard users need a way to select all restorable
entries, clear the restore selection, focus individual restore-entry rows, and
toggle the focused entry without changing the cleanup/restore safety model.

This work must keep package file operations on the existing background job path
and avoid any audio-thread work.

## Decision

Extend `PackageMediaMaintenanceBrowserRows` with keyboard focus metadata and
restore-selection command availability:

- focused selectable row id and row index;
- select-all restore-entry command availability;
- clear restore-entry selection availability;
- focused restore-entry toggle availability and original package-relative path.

Keep cleanup batch navigation on browser Up/Down so existing batch traversal
remains predictable even when restore-entry rows are visible. Add row focus
movement separately through Tab and Shift-Tab. Activate the focused selectable
row with Enter or Space. Add Command/Ctrl+A for selecting all restorable entries
and Escape for clearing the restore selection.

The JUCE `WorkspacePanel` stays generic: it paints the focused row, routes the
keyboard gesture to callbacks, and lets `MainComponent` dispatch through the
existing Package Maintenance browser selection ids.

## Consequences

- Restore-entry selection is reachable without pointer input.
- Batch selection via Up/Down remains unchanged.
- Keyboard command availability is tested in the plain C++ browser-row model.
- Package-busy, conflict-review, partial-failure, restored, and empty-selection
  restore disabled states stay conservative.
- No new dependency is added.

## Follow-Ups

- Add a fuller command-palette/global-shortcut layer before exposing these as
  app-wide commands.
- Add richer conflict-recovery actions before enabling repeat restore for
  conflict or partial-failure batches.
