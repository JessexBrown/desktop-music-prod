<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Manifest Load Symlink Failure Coverage

Acceptance:
- Add focused core coverage for loading a project package whose `manifest.json`
  path is a symlink.
- Fixture-gate the symlink case cleanly when the host cannot create test
  symlinks without elevated permissions.
- Verify load fails without parsing JSON through the symlink, leaves the link
  target untouched, and does not mutate an existing `AppSession` project.
- Preserve missing-manifest, manifest-directory, malformed JSON,
  unsupported-version, schema, and successful load/save coverage.

## 2. Add Project Save Package Symlink Path Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package when the target package
  path or an intermediate parent path is a symlink.
- Fixture-gate the symlink case cleanly when the host cannot create test
  symlinks without elevated permissions.
- Verify save does not follow the symlink, does not create package asset folders
  or manifest temp files, and leaves the symlink target untouched.
- Preserve package-path file rejection, package-directory-creation failure,
  asset-folder failure, temporary-manifest failure, backup failure, commit
  failure, and successful save/load tests.

## 3. Add App Settings Load Symlink Path Coverage

Acceptance:
- Add focused core coverage for loading app settings when the settings path is a
  symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load returns no settings without parsing JSON through the symlink,
  leaves the link target untouched, and keeps caller fallback/default settings
  unchanged.
- Preserve missing-file, load-directory, malformed JSON, unsupported-version,
  reset, and successful load/save coverage.

## 4. Add App Settings Save Symlink Path Failure Coverage

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

## 5. Add Save As Copy Source Symlink Rejection Coverage

Acceptance:
- Add focused Save As copy coverage for source package folders or copied source
  asset entries that are symlinks instead of real directories/files.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify copy fails before target package mutation, leaves the symlink target
  untouched, and reports a human-readable unsupported-source-entry error.
- Preserve target-conflict, source-missing, cancellation, progress, and
  successful copy coverage.
