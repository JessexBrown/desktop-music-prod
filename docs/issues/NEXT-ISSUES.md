<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Load Linked Package Parent Symlink Failure Coverage

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

## 2. Add Project Load Broken Package Parent Symlink Failure Coverage

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

## 3. Add AppSession Load Linked Package Parent Symlink Failure Coverage

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

## 4. Add AppSession Load Broken Package Parent Symlink Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for loading a project package through a
  broken intermediate package parent symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the session load reports failure, leaves the missing symlink target
  uncreated, leaves the current session project unchanged, and does not create a
  temporary manifest through the link.
- Preserve core linked/broken package-parent load rejection, session linked
  package-parent load rejection, manifest symlink and broken-symlink load
  rejection, manifest directory rejection, and successful session save/load
  coverage.

## 5. Add Project Load Direct Package Symlink Failure Coverage

Acceptance:
- Add focused core coverage for loading a project package through a direct
  package-directory symlink to an existing directory.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the path before parsing the linked target manifest,
  preserves the linked target manifest, and does not create a temporary
  manifest through the link.
- Preserve linked/broken package-parent load rejection, manifest symlink and
  broken-symlink load rejection, manifest directory rejection, package save
  symlink rejection, and successful save/load coverage.
