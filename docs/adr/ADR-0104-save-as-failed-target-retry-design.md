<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0104: Save As Failed Target Retry Design

## Status

Accepted.

## Context

ADR-0101 keeps copied target assets after a late `Save As...` manifest failure,
and ADR-0102 exposes `Project > Copy Failed Save As Target` so the user can
locate that kept target package. The next recovery step is a retry workflow for
the final target manifest write, but adding a visible retry command without a
clear overwrite and conflict policy could make a recoverable package ambiguous
or destructive.

The late failure boundary is narrow: package-local assets have already been
copied to the chosen target package, but writing `manifest.json` failed. The
active project package must remain the source package until a retry or a fresh
Save As succeeds.

## Decision

Define a future project-scoped command:

- id: `project.saveAs.retryFailedTargetManifest`
- label: `Retry Failed Save As`
- scope: project

The command is not implemented in this slice. When implemented, it should appear
near `Copy Failed Save As Target` in the Project menu and later in the command
palette. Its status copy must make clear that retry writes only the target
manifest for the kept target package; it does not recopy package assets, delete
assets, quarantine assets, scan plugins, or create a release package.

### Enablement

Enable the retry command only when all of these are true:

- a late Save As failure recorded a kept target package path;
- the completed package-asset copy had copied at least one target asset;
- the active project package is still the original source package;
- no native project chooser is open;
- no package file work is active, including import, relink preparation, Save As
  package copy, timeline preparation, or package media cleanup;
- the in-memory project state is still safe to commit to the kept target.

To make the last condition deterministic, the future implementation should store
a short-lived failed Save As recovery state rather than only a path. That state
should include at least the source package path, target package path, project
snapshot or project revision/signature, and enough package-copy metadata to know
that the kept target matches the manifest being retried. If the project has
mutated in a way that could introduce target-missing package-local assets, clear
or disable retry and ask the user to use a fresh `Save As...` instead.

Clear the recovery state when:

- retry succeeds;
- a new `Save As...` attempt starts;
- Save As succeeds, is cancelled, or fails before the late manifest boundary;
- New or Open switches the active project package;
- the app shuts down.

### Command Boundary

Retry should perform a manifest-only commit through the existing staged project
manifest writer. It must not run on the real-time audio callback and must not
call audio-thread code. The initial implementation may run on the UI command
path only if the manifest write remains small and bounded; if manifests become
large enough to cause visible stalls, wrap the retry in a background job before
shipping the visible command.

On retry success:

- write the target `manifest.json` through the staged writer;
- switch the active package path to the target package;
- clear the failed Save As recovery state;
- refresh browser, inspector, mixer, package-maintenance, and command state;
- show status that the failed Save As target was recovered.

On retry failure:

- keep the active package on the source package;
- keep copied target assets in place;
- keep the failed Save As recovery state available when it is still safe to
  retry again;
- show a human-readable failure reason.

### Overwrite And Conflict Behavior

Retry must preflight the target before writing:

- If `manifest.json` does not exist, retry may attempt the staged manifest
  commit.
- If `manifest.json` already exists as a regular file, retry must not overwrite
  it automatically. Report that the target now contains a manifest and ask the
  user to open it, choose a fresh Save As target, or clean it up manually.
- If `manifest.json` exists as a directory, symlink, device, or other
  non-regular entry, retry must fail with a human-readable conflict and keep the
  active project on the source package.
- If `manifest.json.tmp` exists, the retry may let the staged writer remove or
  replace only that temporary file after the final `manifest.json` path passes
  the checks above.
- If package-local assets referenced by the manifest are missing from the kept
  target package, retry must fail without writing the manifest. The user should
  use a fresh `Save As...` so the package-copy preflight can run again.

Do not delete, quarantine, or rewrite copied target assets automatically during
retry. Any future delete/cleanup affordance must be explicit, confirmable, and
separate from retry.

## Consequences

- The future retry workflow has a small, reviewable boundary: manifest-only
  recovery for a known kept target.
- Users avoid surprise overwrites if the target package has changed since the
  late Save As failure.
- The current non-destructive cleanup policy remains intact.
- Retrying remains outside the real-time audio path.
- A visible retry command is deferred until tests can cover success, target
  manifest conflict, missing target assets, and no-copy-job/no-cleanup behavior.

## Follow-Ups

- Add `project.saveAs.retryFailedTargetManifest` to the app command registry and
  Project menu using the enablement rules above.
- Add deterministic chooser smoke coverage for successful retry after the
  blocking manifest path is removed.
- Add focused core/app coverage for regular-file, directory, symlink or
  non-regular manifest conflicts, missing copied target assets, and stale
  `manifest.json.tmp` cleanup.
