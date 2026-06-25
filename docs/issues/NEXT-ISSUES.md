<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Load Package Path File Settings Isolation Coverage

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

## 2. Add AppSession Load Package Path File Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading from a requested project
  package path occupied by a regular file leaves an isolated app settings file
  unchanged.
- Verify session load reports a human-readable package-path failure without
  parsing JSON, leaves the occupied file unchanged, keeps the current session
  project unchanged, leaves app settings loadable and byte-for-byte unchanged,
  and creates no package folders or `manifest.json.tmp`.
- Preserve project and AppSession package-path file load coverage, project
  package-path file settings-isolation coverage, package path file save
  rejection, direct linked/broken package symlink rejection, linked/broken
  package-parent rejection, manifest directory rejection, manifest
  symlink/broken-symlink rejection, and successful save/load coverage.

## 3. Add Project Load Missing Manifest Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package directory with no
  `manifest.json` leaves an isolated app settings file unchanged.
- Verify load reports a human-readable missing-manifest failure, leaves the
  package directory contents unchanged, leaves app settings loadable and
  byte-for-byte unchanged, and creates no `manifest.json.tmp`.
- Preserve existing missing-manifest recovery coverage, project and AppSession
  package-path file coverage, direct linked/broken package symlink rejection,
  linked/broken package-parent rejection, manifest directory rejection,
  manifest symlink/broken-symlink rejection, and successful save/load coverage.

## 4. Add AppSession Load Missing Manifest Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading a project package directory
  with no `manifest.json` leaves an isolated app settings file unchanged.
- Verify session load reports a human-readable missing-manifest failure, keeps
  the current session project unchanged, leaves the package directory contents
  unchanged, leaves app settings loadable and byte-for-byte unchanged, and
  creates no `manifest.json.tmp`.
- Preserve existing missing-manifest recovery coverage, project missing-manifest
  settings-isolation coverage, project and AppSession package-path file
  coverage, direct linked/broken package symlink rejection, linked/broken
  package-parent rejection, manifest directory rejection, manifest
  symlink/broken-symlink rejection, and successful session save/load coverage.

## 5. Add Project Load Malformed Manifest Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package with malformed
  `manifest.json` leaves an isolated app settings file unchanged.
- Verify load reports a human-readable JSON failure, leaves the malformed
  manifest unchanged, leaves app settings loadable and byte-for-byte unchanged,
  and creates no `manifest.json.tmp`.
- Preserve malformed-manifest recovery coverage, missing-manifest settings
  isolation coverage, project and AppSession package-path file coverage,
  direct linked/broken package symlink rejection, linked/broken package-parent
  rejection, manifest directory rejection, manifest symlink/broken-symlink
  rejection, and successful save/load coverage.
