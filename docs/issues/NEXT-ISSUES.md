<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Load Broken Parent Settings Isolation Coverage

Acceptance:
- Add focused coverage that loading through a broken intermediate package parent
  symlink leaves an isolated app settings file unchanged.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the broken parent path, leaves the missing target
  uncreated, leaves app settings loadable and byte-for-byte unchanged, and does
  not create a temporary manifest through the link.
- Preserve linked/broken package-parent load rejection, direct linked/broken
  package symlink rejection, manifest symlink and broken-symlink settings
  isolation, manifest directory settings isolation, and successful save/load
  coverage.

## 2. Add Project Load Direct Package Symlink Settings Isolation Coverage

Acceptance:
- Add focused coverage that loading through a direct package-directory symlink
  to an existing directory leaves an isolated app settings file unchanged.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects before parsing the linked target manifest, preserves the
  linked target manifest, leaves app settings loadable and byte-for-byte
  unchanged, and creates no temporary manifest through the link.
- Preserve direct linked/broken package symlink rejection, linked/broken
  package-parent settings isolation, manifest symlink and broken-symlink
  settings isolation, manifest directory settings isolation, and successful
  save/load coverage.

## 3. Add Project Load Broken Direct Package Symlink Settings Isolation Coverage

Acceptance:
- Add focused coverage that loading through a direct package-directory symlink
  whose target is missing leaves an isolated app settings file unchanged.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load rejects the broken package symlink, leaves the missing target
  uncreated, leaves app settings loadable and byte-for-byte unchanged, and
  creates no temporary manifest through the link or target path.
- Preserve direct linked/broken package symlink rejection, direct linked
  package settings isolation, linked/broken package-parent settings isolation,
  manifest symlink and broken-symlink settings isolation, manifest directory
  settings isolation, and successful save/load coverage.

## 4. Add AppSession Load Direct Package Symlink Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading through a direct
  package-directory symlink to an existing directory leaves an isolated app
  settings file unchanged.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify session load rejects before parsing the linked target manifest,
  preserves the linked target manifest, keeps the current session project
  unchanged, leaves app settings loadable and byte-for-byte unchanged, and
  creates no temporary manifest through the link.
- Preserve direct linked/broken package symlink rejection, session linked/broken
  direct package symlink rejection, linked/broken package-parent settings
  isolation, manifest symlink and broken-symlink settings isolation, manifest
  directory settings isolation, and successful session save/load coverage.

## 5. Add AppSession Load Broken Direct Package Symlink Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading through a direct
  package-directory symlink whose target is missing leaves an isolated app
  settings file unchanged.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify session load rejects the broken package symlink, leaves the missing
  target uncreated, keeps the current session project unchanged, leaves app
  settings loadable and byte-for-byte unchanged, and creates no temporary
  manifest through the link or target path.
- Preserve direct linked/broken package symlink rejection, session linked/broken
  direct package symlink rejection, AppSession direct package settings
  isolation, linked/broken package-parent settings isolation, manifest symlink
  and broken-symlink settings isolation, manifest directory settings isolation,
  and successful session save/load coverage.
