<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add AppSession Save Linked Asset Folder Symlink Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for saving a project package whose existing
  asset folder path, such as `audio/`, is a symlink to an existing directory.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the session save reports failure, leaves the current session project
  unchanged, preserves the linked target contents, and does not create a project
  temporary manifest.
- Preserve core linked and broken asset-folder symlink rejection, session broken
  asset-folder symlink rejection, session manifest symlink rejection, and
  successful session save/load coverage.

## 2. Add Background Save As Linked Target Symlink Failure Coverage

Acceptance:
- Add focused background Save As package-copy job coverage for a target package
  symlink or intermediate target parent symlink to an existing directory.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the background job reports failure, reaches the failed phase, preserves
  linked target sentinels, and does not copy package assets through the link.
- Preserve plain copy-command target symlink rejection, background broken target
  and source symlink coverage, cancellation progress, and successful background
  copy coverage.

## 3. Add Project Load Linked Package Parent Symlink Failure Coverage

Acceptance:
- Add focused core coverage for loading a project package through an
  intermediate package parent symlink to an existing directory.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the path, preserves the linked target manifest, leaves the
  current session project unchanged, and does not create a temporary manifest
  through the link.
- Preserve manifest symlink and broken-symlink load rejection, manifest
  directory rejection, package save parent symlink rejection, and successful
  save/load coverage.

## 4. Add Project Load Broken Package Parent Symlink Failure Coverage

Acceptance:
- Add focused core coverage for loading a project package through a broken
  intermediate package parent symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the path, leaves the missing symlink target uncreated,
  leaves the current session project unchanged, and does not create a temporary
  manifest through the link.
- Preserve manifest symlink and broken-symlink load rejection, manifest
  directory rejection, linked package-parent load rejection, package save parent
  symlink rejection, and successful save/load coverage.

## 5. Add AppSession Load Linked Package Parent Symlink Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for loading a project package through an
  intermediate package parent symlink to an existing directory.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the session load reports failure, preserves the linked target
  manifest, leaves the current session project unchanged, and does not create a
  temporary manifest through the link.
- Preserve core linked/broken package-parent load rejection, manifest symlink
  and broken-symlink load rejection, manifest directory rejection, and
  successful session save/load coverage.
