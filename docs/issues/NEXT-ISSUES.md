<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Load Missing Manifest Settings Isolation Coverage

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

## 2. Add AppSession Load Missing Manifest Settings Isolation Coverage

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

## 3. Add Project Load Malformed Manifest Settings Isolation Coverage

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

## 4. Add AppSession Load Malformed Manifest Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading a project package with
  malformed `manifest.json` leaves an isolated app settings file unchanged.
- Verify session load reports a human-readable JSON failure, keeps the current
  session project unchanged, leaves the malformed manifest unchanged, leaves app
  settings loadable and byte-for-byte unchanged, and creates no
  `manifest.json.tmp`.
- Preserve malformed-manifest recovery coverage, project malformed-manifest
  settings-isolation coverage, missing-manifest settings-isolation coverage,
  project and AppSession package-path file coverage, direct linked/broken
  package symlink rejection, linked/broken package-parent rejection, manifest
  directory rejection, manifest symlink/broken-symlink rejection, and successful
  session save/load coverage.

## 5. Add Project Load Unsupported Manifest Version Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package with an unsupported
  `manifestVersion` leaves an isolated app settings file unchanged.
- Verify load reports a human-readable version failure, leaves the unsupported
  manifest unchanged, leaves app settings loadable and byte-for-byte unchanged,
  and creates no `manifest.json.tmp`.
- Preserve unsupported-version recovery coverage, malformed-manifest recovery
  coverage, missing-manifest settings-isolation coverage, project and
  AppSession package-path file coverage, direct linked/broken package symlink
  rejection, linked/broken package-parent rejection, manifest directory
  rejection, manifest symlink/broken-symlink rejection, and successful
  save/load coverage.
