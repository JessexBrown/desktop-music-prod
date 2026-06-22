<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0098: Package Maintenance Restore Detail Actions

## Status

Accepted.

## Context

Restore-conflict and partial-failure cleanup batches intentionally block repeat
restore because a second run could overwrite or obscure review evidence. Users
still need a small, keyboard-reachable way to inspect the affected
package-relative path and restore manifest before a fuller recovery flow exists.

## Decision

Extend `PackageMediaMaintenanceBrowserRows` with non-mutating detail-action
metadata for restore entries that need review. Conflict and partial-failure
entry rows receive:

- a copy package-relative path action;
- a restore manifest path action for reveal/copy fallback UI.

Use a separate `restore-detail:` selection id for review rows, distinct from
the existing `restore-entry:` toggle id. This keeps review rows focusable and
activatable without making them restorable selections. The JUCE browser wires
focused review rows to copy the package-relative path through Command/Ctrl+C.
Direct row activation prefers the restore-manifest action: it reveals the
manifest with the platform file browser when the file exists, or copies the
manifest path if reveal is unavailable. Restore execution remains disabled for
review-blocked batches. The JUCE app keeps the clipboard write for users but
the deterministic smoke assertion records the copied value in app state and
checks the visible status copy, avoiding platform clipboard timing as the test
oracle.

## Consequences

- Conflict and partial-failure evidence is reachable from keyboard-focused
  Package Maintenance rows.
- Review actions do not move files, overwrite active package media, parse
  manifests on the UI callback, or run restore again.
- Core tests cover action availability and focused-row mapping for conflict and
  partial-failure rows.
- The hidden JUCE app `--smoke-restore-details` path covers focused review-row
  Command/Ctrl+C copy, distinct status copy, and restore-manifest activation
  fallback without starting cleanup or restore jobs.
- Manifest path evidence is reachable without adding modal review UI.

## Follow-Ups

- Add richer recovery choices, such as rename-assisted restore, before repeat
  restore is considered for review-blocked batches.
