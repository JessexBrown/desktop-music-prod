<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Save Broken Later Asset Folder Symlink Coverage

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

## 2. Add AppSession Save Broken Asset Folder Symlink Failure Coverage

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

## 3. Add App Settings Load Broken Parent Symlink Failure Coverage

Acceptance:
- Add focused core coverage for loading app settings when an intermediate
  settings parent directory is a broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the path, leaves caller fallback settings unchanged,
  leaves the missing symlink target uncreated, and does not create a temporary
  settings file through the link.
- Preserve settings-file symlink load rejection, broken settings-file symlink
  load rejection, directory-path fallback, corruption recovery, and successful
  load/save coverage.

## 4. Add App Settings Load Linked Parent Symlink Failure Coverage

Acceptance:
- Add focused core coverage for loading app settings when an intermediate
  settings parent directory is a symlink to an existing directory.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the path, leaves caller fallback settings unchanged,
  preserves the linked target settings file, and does not create a temporary
  settings file through the link.
- Preserve settings-file symlink load rejection, broken settings-file and
  parent-symlink load rejection, directory-path fallback, corruption recovery,
  and successful load/save coverage.

## 5. Add Background Save As Broken Target Symlink Failure Coverage

Acceptance:
- Add focused background Save As package-copy job coverage for a broken target
  package symlink or broken intermediate target parent symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the background job reports failure, reaches the failed phase, leaves
  the missing symlink target uncreated, and does not copy package assets through
  the target link.
- Preserve plain copy-command broken target symlink coverage, background broken
  source symlink failure coverage, cancellation progress, and successful
  background copy coverage.
