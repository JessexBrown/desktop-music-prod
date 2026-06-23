<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Save As Copy Broken Source Symlink Failure Coverage

Acceptance:
- Add focused Save As copy coverage for source package asset folders or asset
  entries that are broken symlinks.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify copy rejects the source link before target package mutation, leaves the
  missing symlink target uncreated, and reports the existing source
  folder/entry symlink error.
- Preserve Save As source-folder symlink rejection, source-entry symlink
  rejection, broken target symlink rejection, target-conflict, cancellation,
  progress, and successful copy coverage.

## 2. Add Project Save Broken Asset Folder Symlink Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package when an asset folder
  path, such as `audio/`, is a broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save rejects the asset folder path before manifest staging, leaves the
  missing symlink target uncreated, and does not write or remove project
  manifest files through the link.
- Preserve package-path symlink rejection, asset-folder symlink-to-directory
  rejection, temporary-manifest cleanup, manifest-symlink rejection, and
  successful save/load coverage.

## 3. Add Project Save Broken Manifest Symlink Session Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for saving a project package whose existing
  `manifest.json` path is a broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the session save reports failure, leaves the current session project
  unchanged, leaves the missing symlink target uncreated, and removes any stale
  temporary manifest.
- Preserve core broken-manifest-symlink save rejection, session
  manifest-symlink save rejection, temporary-manifest cleanup, and successful
  session save/load coverage.

## 4. Add App Settings Save Broken Parent Symlink Failure Coverage

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

## 5. Add Project Load Manifest Directory Settings Isolation Coverage

Acceptance:
- Add focused session coverage proving project load failures caused by a
  directory at `manifest.json` do not rewrite app settings.
- Verify load rejects the package, leaves the active session project unchanged,
  leaves any isolated app settings file unchanged, and does not create a
  project temporary manifest.
- Preserve manifest-directory load rejection, manifest symlink and
  broken-symlink settings isolation, app settings corruption recovery, and
  successful project load/save coverage.
