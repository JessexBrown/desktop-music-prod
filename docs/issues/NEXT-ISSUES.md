<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add App Settings Empty Path Failure Coverage

Acceptance:
- Add focused core coverage for saving app settings with an empty settings path.
- Verify the save fails before directory creation or temporary-write work.
- Verify the error is human-readable and no settings model state is mutated.
- Preserve the successful settings save/load, reset, commit-failure,
  temporary-write-failure, and directory-creation-failure tests.

## 2. Draft macOS Artifact Signing Policy

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

## 3. Add Save As Retry Symlink Conflict Coverage

Acceptance:
- Add focused retry preflight coverage for `manifest.json` symlink conflicts on
  platforms where symlink creation is available without elevated permissions.
- Skip or fixture-gate the symlink case cleanly when the host cannot create test
  symlinks.
- Verify the symlink target is not overwritten or removed, retry remains
  manifest-only, and the error is human-readable.
- Preserve the existing regular-file, directory, missing-asset, and stale
  temporary-manifest retry coverage.

## 4. Add Project Manifest Load Directory Failure Coverage

Acceptance:
- Add focused core coverage for loading a project package whose `manifest.json`
  path is occupied by a directory.
- Verify load fails with a human-readable missing/unreadable manifest error and
  does not mutate an existing `AppSession` project when routed through session
  loading.
- Preserve malformed JSON, unsupported-version, schema, and missing-manifest
  load failure coverage.

## 5. Add Project Save Package Directory Creation Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package when the package
  directory cannot be created because an intermediate parent path is occupied by
  a regular file.
- Verify the occupied parent file remains unchanged and no package asset folders
  or manifest temp files are created.
- Verify the error is human-readable.
- Preserve package-path file rejection, asset-folder failure, temporary-manifest
  failure, backup failure, commit failure, and successful save/load tests.
