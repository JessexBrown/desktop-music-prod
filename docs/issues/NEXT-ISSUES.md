<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add AppSession Load Invalid Tracks Schema Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading a project package whose
  manifest has a non-array `tracks` field leaves an isolated app settings file
  unchanged.
- Verify session load reports a human-readable tracks schema failure, keeps the
  current session project unchanged, leaves the manifest unchanged, leaves app
  settings loadable and byte-for-byte unchanged, and creates no
  `manifest.json.tmp`.
- Preserve invalid-tracks recovery coverage, project invalid-tracks
  settings-isolation coverage, unsupported-version settings isolation coverage,
  malformed-manifest recovery coverage, missing-manifest settings-isolation
  coverage, project and AppSession package-path file coverage, direct
  linked/broken package symlink rejection, linked/broken package-parent
  rejection, manifest directory rejection, manifest symlink/broken-symlink
  rejection, and successful session save/load coverage.

## 2. Add Project Load Invalid Loop Region Schema Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package whose manifest has a
  non-object `loopRegion` field leaves an isolated app settings file unchanged.
- Verify load reports a human-readable loop-region schema failure, leaves the
  manifest unchanged, leaves app settings loadable and byte-for-byte unchanged,
  and creates no `manifest.json.tmp`.
- Preserve invalid-loop-region recovery coverage, invalid-tracks settings
  isolation coverage, unsupported-version settings isolation coverage,
  malformed-manifest recovery coverage, missing-manifest settings-isolation
  coverage, project and AppSession package-path file coverage, direct
  linked/broken package symlink rejection, linked/broken package-parent
  rejection, manifest directory rejection, manifest symlink/broken-symlink
  rejection, and successful save/load coverage.

## 3. Add AppSession Load Invalid Loop Region Schema Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading a project package whose
  manifest has a non-object `loopRegion` field leaves an isolated app settings
  file unchanged.
- Verify session load reports a human-readable loop-region schema failure,
  keeps the current session project unchanged, leaves the manifest unchanged,
  leaves app settings loadable and byte-for-byte unchanged, and creates no
  `manifest.json.tmp`.
- Preserve invalid-loop-region recovery coverage, project invalid-loop-region
  settings-isolation coverage, invalid-tracks settings isolation coverage,
  unsupported-version settings isolation coverage, malformed-manifest recovery
  coverage, missing-manifest settings-isolation coverage, project and
  AppSession package-path file coverage, direct linked/broken package symlink
  rejection, linked/broken package-parent rejection, manifest directory
  rejection, manifest symlink/broken-symlink rejection, and successful session
  save/load coverage.

## 4. Add Project Load Invalid Loop Region Value Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package whose manifest has an
  enabled `loopRegion` with an invalid length leaves an isolated app settings
  file unchanged.
- Verify load reports a human-readable loop-region value failure, leaves the
  manifest unchanged, leaves app settings loadable and byte-for-byte unchanged,
  and creates no `manifest.json.tmp`.
- Preserve invalid-loop-region recovery coverage, loop-region schema settings
  isolation coverage, invalid-tracks settings isolation coverage,
  unsupported-version settings isolation coverage, malformed-manifest recovery
  coverage, missing-manifest settings-isolation coverage, project and
  AppSession package-path file coverage, direct linked/broken package symlink
  rejection, linked/broken package-parent rejection, manifest directory
  rejection, manifest symlink/broken-symlink rejection, and successful
  save/load coverage.

## 5. Add AppSession Load Invalid Loop Region Value Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading a project package whose
  manifest has an enabled `loopRegion` with an invalid length leaves an
  isolated app settings file unchanged.
- Verify session load reports a human-readable loop-region value failure, keeps
  the current session project unchanged, leaves the manifest unchanged, leaves
  app settings loadable and byte-for-byte unchanged, and creates no
  `manifest.json.tmp`.
- Preserve invalid-loop-region recovery coverage, project invalid-loop-region
  value settings-isolation coverage, loop-region schema settings isolation
  coverage, invalid-tracks settings isolation coverage, unsupported-version
  settings isolation coverage, malformed-manifest recovery coverage,
  missing-manifest settings-isolation coverage, project and AppSession
  package-path file coverage, direct linked/broken package symlink rejection,
  linked/broken package-parent rejection, manifest directory rejection,
  manifest symlink/broken-symlink rejection, and successful session save/load
  coverage.
