<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add App Settings Save Broken Parent Symlink Failure Coverage

Acceptance:
- Add focused core coverage for saving app settings when an intermediate
  settings parent directory is a broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save rejects the parent path before creating the settings file, leaves
  the missing symlink target uncreated, and does not write a temporary settings
  file through the link.
- Preserve settings-path symlink rejection, broken settings-path symlink
  rejection, temporary-symlink cleanup, temporary-write failure, commit failure,
  and successful load/save coverage.

## 2. Add Project Load Manifest Directory Settings Isolation Coverage

Acceptance:
- Add focused session coverage proving project load failures caused by a
  directory at `manifest.json` do not rewrite app settings.
- Verify load rejects the package, leaves the active session project unchanged,
  leaves any isolated app settings file unchanged, and does not create a
  project temporary manifest.
- Preserve manifest-directory load rejection, manifest symlink and
  broken-symlink settings isolation, app settings corruption recovery, and
  successful project load/save coverage.

## 3. Add Background Save As Broken Source Symlink Failure Coverage

Acceptance:
- Add focused background Save As package-copy job coverage for a broken source
  package asset folder or source asset entry symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the background job reports failure, reaches the failed phase, leaves
  the missing symlink target uncreated, and does not mutate the target package.
- Preserve plain copy-command broken source symlink coverage, broken target
  symlink coverage, cancellation progress, and successful background copy
  coverage.

## 4. Add Project Save Broken Later Asset Folder Symlink Coverage

Acceptance:
- Add focused core coverage for saving a project package when a later asset
  folder path, such as `samples/` or `backups/`, is a broken symlink after
  earlier asset folders already exist as regular directories.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save rejects the named asset folder before manifest staging, leaves the
  missing symlink target uncreated, preserves earlier asset directories, and
  does not write or remove project manifest files through the link.
- Preserve broken `audio/` asset-folder symlink rejection, package-path symlink
  rejection, asset-folder symlink-to-directory rejection, temporary-manifest
  cleanup, manifest-symlink rejection, and successful save/load coverage.

## 5. Add AppSession Save Broken Asset Folder Symlink Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for saving a project package whose existing
  asset folder path, such as `audio/`, is a broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the session save reports failure, leaves the current session project
  unchanged, leaves the missing symlink target uncreated, and does not create a
  project temporary manifest.
- Preserve core broken asset-folder symlink rejection, session manifest symlink
  and broken-symlink save rejection, temporary-manifest cleanup, and successful
  session save/load coverage.
