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
- a restore manifest path action for future reveal/copy UI.

Use a separate `restore-detail:` selection id for review rows, distinct from
the existing `restore-entry:` toggle id. This keeps review rows focusable and
activatable without making them restorable selections. The JUCE browser wires
focused review rows to copy the package-relative path through Command/Ctrl+C
or direct row activation, while restore execution remains disabled for
review-blocked batches.

## Consequences

- Conflict and partial-failure evidence is reachable from keyboard-focused
  Package Maintenance rows.
- Review actions do not move files, overwrite active package media, parse
  manifests on the UI callback, or run restore again.
- Core tests cover action availability for conflict and partial-failure rows.
- Manifest reveal UI remains a follow-up; the model carries the path metadata
  without adding platform shell integration yet.

## Follow-Ups

- Add a platform-aware reveal-manifest action or copy-manifest-path fallback.
- Add richer recovery choices, such as rename-assisted restore, before repeat
  restore is considered for review-blocked batches.
