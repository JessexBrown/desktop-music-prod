<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Save Manifest Symlink Session Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for saving a project package whose existing
  `manifest.json` path is a symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the session save reports failure, leaves the current project/session
  package state unchanged, preserves the linked manifest target, and removes
  any stale temporary manifest.
- Preserve core project-save manifest symlink rejection, broken-symlink
  coverage, temporary-manifest cleanup, and successful session save/load
  coverage.

## 2. Add App Settings Save Broken Temporary Symlink Cleanup Coverage

Acceptance:
- Add focused core coverage for saving app settings when a stale
  `settings.json.tmp` path is a broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save removes the stale temporary link, leaves the missing symlink
  target uncreated, writes a real final settings file, and does not leave a
  temporary settings file behind.
- Preserve settings-path symlink rejection, broken settings-path symlink
  rejection, temporary-symlink-to-file cleanup, temporary-write failure, commit
  failure, and successful load/save coverage.

## 3. Add Project Load Manifest Symlink Settings Isolation Coverage

Acceptance:
- Add focused session coverage proving project load failures caused by
  symlinked or broken-symlinked `manifest.json` paths do not rewrite app
  settings.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the package, leaves the active session project unchanged,
  leaves any isolated app settings file unchanged, and does not create a
  project temporary manifest.
- Preserve existing project load symlink rejection, app settings corruption
  recovery, and successful project load/save coverage.

## 4. Add Save As Copy Broken Source Symlink Failure Coverage

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

## 5. Add Project Save Broken Asset Folder Symlink Failure Coverage

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
