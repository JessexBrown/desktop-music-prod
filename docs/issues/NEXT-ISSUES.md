<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Save Temporary Manifest Write Failure Coverage

Acceptance:
- Add focused core coverage for a project save failure before
  `manifest.json.tmp` can be opened or written.
- Verify the existing `manifest.json`, when present, remains unchanged.
- Verify the occupied temporary path remains unchanged and the error is
  human-readable.
- Preserve the successful project save/load, previous-backup success/failure,
  asset-folder failure, and commit-failure tests.

## 2. Add Project Save Package Path File Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package to a path already
  occupied by a regular file.
- Verify the occupied file remains unchanged and no package asset folders or
  manifest temp files are created.
- Verify the error is human-readable.
- Preserve the successful project save/load and existing project save failure
  tests.

## 3. Add App Settings Empty Path Failure Coverage

Acceptance:
- Add focused core coverage for saving app settings with an empty settings path.
- Verify the save fails before directory creation or temporary-write work.
- Verify the error is human-readable and no settings model state is mutated.
- Preserve the successful settings save/load, reset, commit-failure,
  temporary-write-failure, and directory-creation-failure tests.

## 4. Draft macOS Artifact Signing Policy

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

## 5. Add Save As Retry Symlink Conflict Coverage

Acceptance:
- Add focused retry preflight coverage for `manifest.json` symlink conflicts on
  platforms where symlink creation is available without elevated permissions.
- Skip or fixture-gate the symlink case cleanly when the host cannot create test
  symlinks.
- Verify the symlink target is not overwritten or removed, retry remains
  manifest-only, and the error is human-readable.
- Preserve the existing regular-file, directory, missing-asset, and stale
  temporary-manifest retry coverage.
