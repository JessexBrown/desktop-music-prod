<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add AppSession Load Linked Parent Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading through a linked intermediate
  package parent symlink leaves an isolated app settings file unchanged.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify session load rejects the linked parent path, preserves the linked
  target manifest, keeps the current session project unchanged, leaves app
  settings loadable and byte-for-byte unchanged, and creates no temporary
  manifest through the link.
- Preserve linked/broken package-parent load rejection, project linked/broken
  package-parent settings isolation, direct linked/broken package symlink
  rejection, direct package settings isolation, manifest symlink and
  broken-symlink settings isolation, manifest directory settings isolation, and
  successful session save/load coverage.

## 2. Add AppSession Load Broken Parent Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading through a broken intermediate
  package parent symlink leaves an isolated app settings file unchanged.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify session load rejects the broken parent path, leaves the missing target
  uncreated, keeps the current session project unchanged, leaves app settings
  loadable and byte-for-byte unchanged, and creates no temporary manifest
  through the link.
- Preserve linked/broken package-parent load rejection, project linked/broken
  package-parent settings isolation, direct linked/broken package symlink
  rejection, direct package settings isolation, manifest symlink and
  broken-symlink settings isolation, manifest directory settings isolation, and
  successful session save/load coverage.

## 3. Add Project Load Package Path File Failure Coverage

Acceptance:
- Add focused core coverage for loading from a requested project package path
  that is occupied by a regular file.
- Verify load reports a human-readable package-path failure without parsing
  JSON, leaves the occupied file unchanged, and creates no package folders or
  `manifest.json.tmp`.
- Preserve successful save/load coverage, package path file save rejection,
  direct linked/broken package symlink rejection, linked/broken package-parent
  rejection, manifest directory rejection, and manifest symlink/broken-symlink
  rejection.

## 4. Add AppSession Load Package Path File Failure Coverage

Acceptance:
- Add focused `AppSession` coverage for loading from a requested project package
  path that is occupied by a regular file.
- Verify load reports a human-readable package-path failure without parsing
  JSON, leaves the occupied file unchanged, keeps the current session project
  unchanged, and creates no package folders or `manifest.json.tmp`.
- Preserve successful session save/load coverage, project package-path file
  load coverage, package path file save rejection, direct linked/broken package
  symlink rejection, linked/broken package-parent rejection, manifest directory
  rejection, and manifest symlink/broken-symlink rejection.

## 5. Add Project Load Package Path File Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading from a requested project package path
  occupied by a regular file leaves an isolated app settings file unchanged.
- Verify load reports a human-readable package-path failure without parsing
  JSON, leaves the occupied file unchanged, leaves app settings loadable and
  byte-for-byte unchanged, and creates no package folders or
  `manifest.json.tmp`.
- Preserve project and AppSession package-path file load coverage, package path
  file save rejection, direct linked/broken package symlink rejection,
  linked/broken package-parent rejection, manifest directory rejection, manifest
  symlink/broken-symlink rejection, and successful save/load coverage.
