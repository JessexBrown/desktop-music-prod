<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add App Settings Save Broken Symlink Coverage

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

## 2. Add Project Save Broken Manifest Symlink Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package when an existing
  `manifest.json` path is a broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save rejects the manifest symlink before backup or commit, leaves the
  missing symlink target uncreated, removes any staged temporary manifest, and
  keeps any existing project package folders untouched.
- Preserve package-path symlink rejection, asset-folder symlink rejection,
  manifest-symlink-to-file rejection, temporary-manifest failure, backup
  failure, commit failure, and successful save/load coverage.

## 3. Add Save As Copy Broken Target Symlink Failure Coverage

Acceptance:
- Add focused Save As copy coverage for target package paths or intermediate
  target parent directories that are broken symlinks.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify copy fails before package asset mutation, leaves the missing symlink
  target uncreated, and reports a human-readable target-directory symlink
  error.
- Preserve Save As target-parent symlink rejection, source-symlink rejection,
  target-conflict, source-missing, cancellation, progress, and successful copy
  coverage.

## 4. Add Project Save Broken Temporary Manifest Symlink Cleanup Coverage

Acceptance:
- Add focused core coverage for saving a project package when a stale
  `manifest.json.tmp` path is a broken symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify save removes the stale temporary link, leaves the missing symlink
  target uncreated, writes the real final manifest, and does not leave a
  staged temporary manifest behind.
- Preserve temporary-manifest symlink-to-file cleanup, manifest-symlink
  rejection, temporary-manifest write/commit failure, backup failure, and
  successful save/load coverage.

## 5. Add Project Save Manifest Symlink Session Failure Coverage

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
