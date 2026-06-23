<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add App Settings Temporary Symlink Cleanup Coverage

Acceptance:
- Add focused core coverage for saving app settings when a stale
  `settings.json.tmp` path is a symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save does not follow the temporary symlink, leaves the temporary
  symlink target untouched, commits only the real settings file, and removes the
  stale temporary link.
- Preserve app-settings save-path symlink rejection, empty-path,
  directory-creation failure, temporary-write failure, commit failure,
  load-directory, and successful load/save coverage.

## 2. Add Save As Copy Target Parent Symlink Failure Coverage

Acceptance:
- Add focused Save As copy coverage for target package paths or intermediate
  target parent directories that are symlinks.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify copy fails before package asset mutation, leaves the symlink target
  untouched, and reports a human-readable target-directory symlink error.
- Preserve Save As source-symlink rejection, target-conflict, source-missing,
  cancellation, progress, and successful copy coverage.

## 3. Add Project Save Temporary Manifest Symlink Cleanup Coverage

Acceptance:
- Add focused core coverage for saving a project package when a stale
  `manifest.json.tmp` path is a symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save does not follow the temporary symlink, leaves the symlink target
  untouched, writes the real final manifest, and removes the stale temporary
  link.
- Preserve project save package-path symlink rejection, asset-folder failure,
  temporary-manifest write/commit failure, backup failure, and successful
  save/load coverage.

## 4. Add Project Save Manifest Symlink Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package when an existing
  `manifest.json` path is a symlink to a file.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save rejects the manifest symlink before backup or commit, leaves the
  symlink target untouched, removes any staged temporary manifest, and keeps
  the previous real manifest state intact when present.
- Preserve package-path symlink rejection, asset-folder symlink rejection,
  temporary-manifest failure, backup failure, commit failure, and successful
  save/load coverage.

## 5. Add App Settings Save Broken Symlink Coverage

Acceptance:
- Add focused core coverage for saving app settings when `settings.json` is a
  broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save rejects the settings symlink before writing a temporary settings
  file, leaves the missing symlink target uncreated, and leaves fallback/caller
  settings unchanged.
- Preserve app-settings save symlink-target rejection, parent-symlink
  rejection, empty-path, directory-creation failure, temporary-write failure,
  commit failure, load-directory, and successful load/save coverage.
