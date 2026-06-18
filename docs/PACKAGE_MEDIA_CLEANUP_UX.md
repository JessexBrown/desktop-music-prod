<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Package Media Cleanup UX

## Purpose

Package media cleanup helps musicians reclaim project-package space without
making recovery feel dangerous. The first visible workflow should make it clear
that cleanup moves unused package-owned media to a recoverable Project Media
Trash, not to permanent deletion.

This note translates the existing inventory, quarantine, restore-manifest,
restore-command, and background-job primitives into the user-facing flow for a
future JUCE UI slice.

## Goals

- Keep cleanup explicit, reversible, and understandable.
- Give missing-media and restore conflicts clear recovery paths.
- Use non-modal status messages and panel affordances instead of blocking the
  workspace.
- Keep inventory, quarantine, and restore work on background jobs.
- Protect current manifests, previous manifest backups, live undo/redo history,
  active staging, and in-flight package work.

## Non-Goals

- No permanent deletion or retention policy.
- No platform trash integration.
- No plugin scanning or plugin cleanup.
- No automatic cleanup during save, open, import, relink, undo, redo, waveform
  regeneration, or timeline playback preparation.
- No mutation from the real-time audio callback.

## Entry Points

- Browser: a Project Assets maintenance row labeled `Review Unused Media`.
- Inspector: when a selected imported clip is missing package-local media and a
  matching quarantine manifest may restore it, show `Review Restore Options`.
- Status bar: when a background inventory detects reviewable cleanup or restore
  states, expose a compact `Review` action.
- Future command palette: `Review Project Media`.

All entry points open the same non-modal Package Maintenance view so users do
not need to remember where cleanup started.

## Primary Cleanup Flow

1. The user opens Package Maintenance from the browser, inspector, status bar,
   or command palette.
2. The app starts a background inventory job.
3. The view shows a summary of unreferenced audio files, waveform summaries,
   stale staging directories, protected references, missing references, and
   unsafe references.
4. Quarantine-safe candidates are selected by default. Protected, unsafe,
   missing, and active-work items remain visible but cannot be selected.
5. The user confirms `Move to Project Media Trash`.
6. The background cleanup job runs preflight again before moving anything.
7. Completion refreshes browser and inspector affordances without changing the
   current project manifest.

The confirmation title should be:

```text
Move unused package media to Project Media Trash?
```

The confirmation body should say:

```text
Rabbington Studio will move unused package-owned media into this project's
recoverable media trash. Referenced clips, previous manifest backups, active
staging work, and unsafe paths will not be moved. Nothing is permanently
deleted.
```

Primary action: `Move to Media Trash`

Secondary action: `Cancel`

## Restore Flow

Package Maintenance lists cleanup batches found under
`backups/media-trash/<cleanup-id>/restore-manifest.json`.
`PackageMediaCleanupBatchDiscovery` provides the first non-UI model for this
list by validating cleanup ids, loading safe restore manifests, attaching
status output, and surfacing invalid or unreadable batches as review issues.

For each cleanup batch, the UI shows:

- cleanup date and id;
- total moved items;
- restored item count;
- conflict count;
- partial-failure warning count;
- a short status label from the restore manifest state.

The user may restore all unrestored items or select individual original
package-relative paths. Restore never overwrites active package media. If an
original path is occupied, the item stays in quarantine and the batch enters a
restore-conflict state.

## Status Mapping

| Engine or model state | Non-modal status text | Browser affordance | Inspector affordance |
| --- | --- | --- | --- |
| Inventory running | Scanning project media... | Show progress row. | Keep current selection visible. |
| Active package work | Finish the current package operation before cleaning project media. | Disable cleanup action. | Show passive notice only. |
| Unsafe references | Resolve unsafe media references before cleanup. | Show warning count; disable those rows. | Link selected clip to media issue when relevant. |
| Missing references | Some package media is missing. Review restore options before cleanup. | Show missing count and restore entry point. | Show `Review Restore Options` for selected affected clips. |
| No candidates | No unused package media to clean up. | Show complete state. | No special action. |
| Preflight ready | Ready to move unused package media to Project Media Trash. | Enable confirmation action. | Show selected clip unaffected unless it is part of a listed issue. |
| Quarantine running | Moving unused media to Project Media Trash... | Show progress and cancellation before mutation handoff. | Keep editing available unless the selected clip is waiting on package work. |
| Quarantine completed | Moved unused media to Project Media Trash. Restore is available from Package Maintenance. | Show cleanup batch in restore list. | Clear cleanup warning for unaffected clips. |
| Cleanup failed | Cleanup needs review. No files were permanently deleted. | Show recoverable failure row and manifest path when available. | Keep current clip state unchanged. |
| Restore running | Restoring project media... | Show progress row on selected cleanup batch. | Keep missing-media notice until restore completes. |
| Restore completed | Restored project media. | Mark batch restored. | Refresh selected missing-media state. |
| Restore conflict | Some files were not restored because their original paths are occupied. | Show conflict count and keep batch reviewable. | Show conflict notice for selected affected clips. |
| Partial failure | Restore needs review. No active package media was overwritten. | Show warning count and manifest path. | Preserve missing-media notice when the selected clip still lacks media. |
| Cancelled before mutation | Cleanup was cancelled before moving media. | Return to inventory result. | Keep current clip state unchanged. |

The visible status copy may add counts when space allows, for example
`Moved 6 items to Project Media Trash.`

## Conflict and Recovery Language

Restore conflicts should avoid blame and avoid asking the user to choose a
destructive overwrite. Recommended text:

```text
This file was not restored because another package file already exists at the
original path. The quarantined copy is still available.
```

Partial failures should mention that the restore manifest is the recovery
record:

```text
Rabbington Studio could not complete every restore step. The restore manifest
has been updated so this batch can be reviewed again.
```

Future UI may add reveal-in-folder and rename-assisted recovery actions, but
the first implementation should only show the conflict state and preserve both
files.

## Accessibility and Interaction

- The Package Maintenance view is non-modal and keyboard reachable.
- Candidate rows expose name, media kind, original path, size when known, and
  status as text, not color alone.
- Progress text remains readable by assistive technology.
- Confirmation focus starts on `Cancel`; keyboard users can reach
  `Move to Media Trash` explicitly.
- Status messages are short enough for the top/status bar, while the browser
  and inspector can show the longer recovery language.

## Implementation Boundary

The first UI implementation should call the existing background cleanup job and
plain C++ command models. It should not move files on the UI thread, scan
plugins, delete quarantined media, invent retention rules, or mutate project
state from the audio callback.

`PackageMediaCleanupStatus` is the first testable status model slice. It turns
inventory, preflight, quarantine, restore, conflict, partial-failure, and
cancellation states into stable status text, severity, and target affordance
identifiers for the browser, inspector, and status bar.

`PackageMediaCleanupBatchDiscovery` is the first testable restore-list model
slice. It keeps visible UI out of scope while defining how cleanup batches are
found and how corrupt or suspicious batches remain reviewable.

`PackageMediaMaintenanceViewModel` combines inventory status, discovered
cleanup batches, discovery issues, selected-batch fallback, and restore action
enablement for the future non-modal Package Maintenance view. Conflict and
partial-failure batches stay selectable for review, but repeat restore is kept
disabled until recovery actions are designed.

The first visible JUCE surface is a read-only browser summary backed by a
background package scan. It shows media status, candidate counts, cleanup batch
rows, selected batch status, restore readiness, and discovery issue count
without exposing cleanup or restore execution.

Cleanup batch rows in that browser summary are selectable by mouse and by
keyboard Up/Down when the browser has focus. Selection changes the displayed
selected batch and restore readiness immediately, but it still does not run
cleanup, restore, deletion, retention, plugin scanning, or package file
mutation.

The browser summary now exposes a guarded Restore affordance after a package
maintenance snapshot exists. It is enabled only for selected cleanup batches
that the maintenance view model marks restorable. Disabled states keep their
reason visible in the browser/status text. Activated restore work runs through
the background package media cleanup job; the UI never moves package files
directly.
