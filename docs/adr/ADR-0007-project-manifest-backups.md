<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0007: Project Manifest Backups

## Status

Accepted for the v0.1 prototype.

## Context

The project package strategy includes a `backups/` folder. Milestone 2 calls for
an autosave/backups design, and early project save/load reliability matters more
than broad editing features.

The first implementation does not yet have a background autosave service, dirty
state tracking, or recovery UI. It can still protect the previous manifest when a
user saves over an existing project package.

## Decision

When `ProjectModel::savePackage` writes to a package that already contains a
regular `manifest.json`, copy that file to:

```text
backups/manifest.previous.json
```

Then write the new `manifest.json`.

If the previous manifest backup cannot be created, fail the save before
overwriting the current manifest. If a staged `manifest.json.tmp` was already
created for the attempted save, remove that staged manifest as part of the
failure path. Keep this behavior in `projectname_core` so it is shared by the
JUCE app, fallback verification app, and tests.

This is a deterministic last-good-manifest backup, not full autosave.

## Consequences

- A second save preserves the immediately previous manifest state.
- Tests can verify backup behavior without a live app or audio device.
- The backup filename is intentionally stable for the prototype and may later be
  replaced by timestamped autosaves or rotating backups.
- Project load still reads `manifest.json`; recovery UI is a future feature.

## Follow-Ups

- Add dirty-state tracking in `AppSession`.
- Add timestamped autosave files once project editing becomes interactive.
- Add recovery UI that can list and restore backups.
