<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add App Settings Save Symlink Path Failure Coverage

Acceptance:
- Add focused core coverage for saving app settings when the settings file path
  or an intermediate parent path is a symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save does not follow the symlink, does not create temporary settings
  files through the linked path, leaves the symlink target untouched, and reports
  a human-readable error.
- Preserve empty-path, directory-creation failure, temporary-write failure,
  commit failure, load-directory, and successful load/save coverage.

## 2. Add Save As Copy Source Symlink Rejection Coverage

Acceptance:
- Add focused Save As copy coverage for source package folders or copied source
  asset entries that are symlinks instead of real directories/files.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify copy fails before target package mutation, leaves the symlink target
  untouched, and reports a human-readable unsupported-source-entry error.
- Preserve target-conflict, source-missing, cancellation, progress, and
  successful copy coverage.

## 3. Add Project Manifest Load Broken Symlink Coverage

Acceptance:
- Add focused core coverage for loading a project package whose `manifest.json`
  path is a broken symlink.
- Fixture-gate the symlink case cleanly when the host cannot create test
  symlinks without elevated permissions.
- Verify load fails as a symlink rejection rather than a missing manifest,
  does not create the broken target, and does not mutate an existing
  `AppSession` project.
- Preserve missing-manifest, manifest-directory, manifest-symlink-target,
  malformed JSON, unsupported-version, schema, and successful load/save
  coverage.

## 4. Add Project Save Asset Folder Symlink Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package when an existing asset
  folder path such as `audio/` is a symlink.
- Fixture-gate the symlink case cleanly when the host cannot create test
  symlinks without elevated permissions.
- Verify save does not follow the symlink, does not write a manifest or
  manifest temp file, and leaves the linked asset target untouched.
- Preserve package-path symlink rejection, package-path file rejection,
  package-directory-creation failure, asset-folder file failure,
  temporary-manifest failure, backup failure, commit failure, and successful
  save/load tests.

## 5. Add App Settings Load Broken Symlink Coverage

Acceptance:
- Add focused core coverage for loading app settings when the settings path is a
  broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load reports a symlink rejection rather than treating the settings as
  missing, does not create the broken target, and keeps caller fallback/default
  settings unchanged.
- Preserve app-settings load symlink-target rejection, missing-file,
  load-directory, malformed JSON, unsupported-version, reset, and successful
  load/save coverage.
