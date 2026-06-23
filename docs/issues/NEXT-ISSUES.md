<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Save As Failed Target Retry Command

Acceptance:
- Add `project.saveAs.retryFailedTargetManifest` using ADR-0104's enablement,
  manifest-only retry, and overwrite/conflict rules.
- Expose it near `Copy Failed Save As Target` without deleting, quarantining, or
  recopying target package assets.
- Cover successful retry after the blocking manifest path is removed, existing
  target `manifest.json` conflict, missing copied target assets, and stale
  `manifest.json.tmp` behavior.
- Verify retry does not start a Save As package-copy job and does not touch the
  real-time audio path.

## 2. Add Project Save Temporary Manifest Write Failure Coverage

Acceptance:
- Add focused core coverage for a project save failure before
  `manifest.json.tmp` can be opened or written.
- Verify the existing `manifest.json`, when present, remains unchanged.
- Verify the occupied temporary path remains unchanged and the error is
  human-readable.
- Preserve the successful project save/load, previous-backup success/failure,
  asset-folder failure, and commit-failure tests.

## 3. Add Project Save Package Path File Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package to a path already
  occupied by a regular file.
- Verify the occupied file remains unchanged and no package asset folders or
  manifest temp files are created.
- Verify the error is human-readable.
- Preserve the successful project save/load and existing project save failure
  tests.

## 4. Add App Settings Empty Path Failure Coverage

Acceptance:
- Add focused core coverage for saving app settings with an empty settings path.
- Verify the save fails before directory creation or temporary-write work.
- Verify the error is human-readable and no settings model state is mutated.
- Preserve the successful settings save/load, reset, commit-failure,
  temporary-write-failure, and directory-creation-failure tests.

## 5. Draft macOS Artifact Signing Policy

Acceptance:
- Add a focused ADR for macOS app artifact, signing, notarization, and installer
  policy before any macOS package upload is enabled.
- Clarify whether unsigned debug `.app` bundles are allowed in CI, what user
  messaging they require, and what must wait for signed/notarized release
  packaging.
- Confirm that the policy does not bundle plugins, presets, samples, commercial
  sounds, logos, or proprietary assets.
- Do not upload a macOS artifact, add signing secrets, notarize, or create an
  installer in the same change.
