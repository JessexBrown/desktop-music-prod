<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Load Direct Package Symlink Failure Coverage

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

## 2. Add Project Load Broken Direct Package Symlink Failure Coverage

Acceptance:
- Add focused core coverage for loading a project package through a direct
  package-directory symlink whose target is missing.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the symlink path, leaves the missing target uncreated, and
  does not create a temporary manifest through the link.
- Preserve linked/broken package-parent load rejection, direct linked package
  symlink rejection, manifest symlink and broken-symlink load rejection,
  manifest directory rejection, package save symlink rejection, and successful
  save/load coverage.

## 3. Add AppSession Load Direct Package Symlink Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for loading a project package through a
  direct package-directory symlink to an existing directory.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the session load reports failure before parsing the linked target
  manifest, preserves the linked target manifest, leaves the current session
  project unchanged, and does not create a temporary manifest through the link.
- Preserve core direct linked/broken package symlink rejection,
  linked/broken package-parent load rejection, manifest symlink and
  broken-symlink load rejection, manifest directory rejection, and successful
  session save/load coverage.

## 4. Add AppSession Load Broken Direct Package Symlink Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for loading a project package through a
  direct package-directory symlink whose target is missing.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify the session load reports failure, leaves the missing target uncreated,
  leaves the current session project unchanged, and does not create a temporary
  manifest through the link.
- Preserve core direct linked/broken package symlink rejection, session direct
  linked package symlink rejection, linked/broken package-parent load rejection,
  manifest symlink and broken-symlink load rejection, manifest directory
  rejection, and successful session save/load coverage.

## 5. Add Project Load Linked Parent Settings Isolation Coverage

Acceptance:
- Add focused coverage that loading through a linked intermediate package parent
  symlink leaves an isolated app settings file unchanged.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the linked parent path, preserves the linked target
  manifest, leaves app settings loadable and byte-for-byte unchanged, and does
  not create a temporary manifest through the link.
- Preserve linked/broken package-parent load rejection, direct linked/broken
  package symlink rejection, manifest symlink and broken-symlink settings
  isolation, manifest directory settings isolation, and successful save/load
  coverage.
