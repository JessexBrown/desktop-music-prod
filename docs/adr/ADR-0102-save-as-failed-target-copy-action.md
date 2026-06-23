<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0102: Save As Failed Target Copy Action

## Status

Accepted.

## Context

ADR-0101 keeps copied target assets after a late `Save As...` manifest failure
instead of deleting or quarantining them automatically. That preserves recovery
data, but status text alone is awkward when the selected target package path is
long or hard to find in the file manager.

The first recovery affordance should stay non-destructive and should not start
another package operation. It also must stay outside the real-time audio path.

## Decision

After a `Save As...` package asset copy completes, but the final target
`manifest.json` write fails, the app records the selected target package path as
a short-lived UI recovery state. The Project menu exposes `Copy Failed Save As
Target` while that path is available.

Activating the command copies the kept target package path to the system
clipboard and updates the status text. It does not retry the manifest save,
delete files, quarantine files, scan packages, or start a background Save As
copy job. The remembered path is cleared when a new Save As attempt starts,
when Save As succeeds, when Save As is cancelled or fails before the late
manifest write boundary, or when the app switches to another project package.

The hidden project chooser smoke test dispatches this command after the
post-copy manifest failure fixture and asserts the copied path/status without
opening native choosers.

## Consequences

- Users have a concrete way to recover the kept target location after the late
  failure boundary.
- The recovery action is safe to expose before richer retry/delete workflows
  exist.
- The command registry now includes a stable project-scoped command id for
  future command palette or shortcut work.
- No proprietary assets, samples, presets, plugins, or new dependencies are
  added.

## Follow-Ups

- ADR-0104 documents the retry-manifest-save workflow and overwrite/conflict
  behavior for a kept failed Save As target before a visible retry command is
  added.
