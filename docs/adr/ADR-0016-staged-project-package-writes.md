<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0016: Staged Project Package Writes

## Status

Accepted for the v0.1 prototype.

## Context

Project packages must stay recoverable as audio import grows beyond tiny test
files. The previous import path copied the source WAV directly into `audio/`
and then saved the manifest. If cancellation or a later failure happened after
copy began, the package could be left with an orphaned imported asset.

Project saves also wrote `manifest.json` directly. The backup file protected the
previous manifest, but the visible manifest was not staged before replacement.

## Decision

Stage package writes for the prototype import path:

- copy imported audio into a hidden `.projectname-staging/` folder first;
- report staged-copy byte progress through `ProjectAudioImportProgress`;
- honor cancellation during staged copy and remove the staging folder;
- move the staged audio file into `audio/` only after copy completes;
- remove the committed audio file if clip attachment or manifest save fails;
- write project manifests to `manifest.json.tmp` before replacing
  `manifest.json`;
- remove temporary manifest files after successful saves.

The staged copy and manifest write code runs only on non-realtime project
save/import paths. The audio callback is not involved.

## Consequences

- Cancelled staged-copy imports do not leave partial audio assets or manifests.
- Successful imports clean up `.projectname-staging/`.
- Background import progress can show copied and total bytes.
- Manifest replacement is more resilient because the JSON is fully written to a
  temporary file before the visible manifest is overwritten.
- WAV decode is cancellable as recorded in ADR-0017; package mutation still
  begins only after decode and staged copy complete.

## Follow-Ups

- Replace same-package file moves/copies with platform-specific atomic replace
  helpers if later reliability testing shows a need.
- Add a package integrity scan for orphaned assets once delete/edit workflows
  exist.
