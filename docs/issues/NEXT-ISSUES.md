<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Load Malformed Manifest Settings Isolation Coverage

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

## 2. Add AppSession Load Malformed Manifest Settings Isolation Coverage

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

## 3. Add Project Load Unsupported Manifest Version Settings Isolation Coverage

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

## 4. Add AppSession Load Unsupported Manifest Version Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading a project package with an
  unsupported `manifestVersion` leaves an isolated app settings file unchanged.
- Verify session load reports a human-readable version failure, keeps the
  current session project unchanged, leaves the unsupported manifest unchanged,
  leaves app settings loadable and byte-for-byte unchanged, and creates no
  `manifest.json.tmp`.
- Preserve unsupported-version recovery coverage, project unsupported-version
  settings-isolation coverage, malformed-manifest recovery coverage,
  missing-manifest settings-isolation coverage, project and AppSession
  package-path file coverage, direct linked/broken package symlink rejection,
  linked/broken package-parent rejection, manifest directory rejection, manifest
  symlink/broken-symlink rejection, and successful session save/load coverage.

## 5. Add Project Load Invalid Tracks Schema Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package whose manifest has a
  non-array `tracks` field leaves an isolated app settings file unchanged.
- Verify load reports a human-readable tracks schema failure, leaves the
  manifest unchanged, leaves app settings loadable and byte-for-byte unchanged,
  and creates no `manifest.json.tmp`.
- Preserve invalid-tracks recovery coverage, unsupported-version settings
  isolation coverage, malformed-manifest recovery coverage, missing-manifest
  settings-isolation coverage, project and AppSession package-path file
  coverage, direct linked/broken package symlink rejection, linked/broken
  package-parent rejection, manifest directory rejection, manifest
  symlink/broken-symlink rejection, and successful save/load coverage.
