<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Load Invalid Devices Schema Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package whose manifest has a
  track with a non-array `devices` field leaves an isolated app settings file
  unchanged.
- Verify load reports a human-readable devices schema failure, leaves the
  manifest unchanged, leaves app settings loadable and byte-for-byte unchanged,
  and creates no `manifest.json.tmp`.
- Preserve invalid-devices recovery coverage, loop-region value/schema settings
  isolation coverage, invalid-tracks settings isolation coverage,
  unsupported-version settings isolation coverage, malformed-manifest recovery
  coverage, missing-manifest settings-isolation coverage, project and
  AppSession package-path file coverage, direct linked/broken package symlink
  rejection, linked/broken package-parent rejection, manifest directory
  rejection, manifest symlink/broken-symlink rejection, and successful
  save/load coverage.

## 2. Add AppSession Load Invalid Devices Schema Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading a project package whose
  manifest has a track with a non-array `devices` field leaves an isolated app
  settings file unchanged.
- Verify session load reports a human-readable devices schema failure, keeps the
  current session project unchanged, leaves the manifest unchanged, leaves app
  settings loadable and byte-for-byte unchanged, and creates no
  `manifest.json.tmp`.
- Preserve invalid-devices recovery coverage, project invalid-devices schema
  settings isolation coverage, loop-region value/schema settings isolation
  coverage, invalid-tracks settings isolation coverage, unsupported-version
  settings isolation coverage, malformed-manifest recovery coverage,
  missing-manifest settings-isolation coverage, project and AppSession
  package-path file coverage, direct linked/broken package symlink rejection,
  linked/broken package-parent rejection, manifest directory rejection, manifest
  symlink/broken-symlink rejection, and successful session save/load coverage.

## 3. Add Project Load Invalid Clips Schema Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package whose manifest has a
  track with a non-array `clips` field leaves an isolated app settings file
  unchanged.
- Verify load reports a human-readable clips schema failure, leaves the manifest
  unchanged, leaves app settings loadable and byte-for-byte unchanged, and
  creates no `manifest.json.tmp`.
- Preserve clips schema rejection behavior, invalid-devices settings
  isolation coverage, loop-region value/schema settings isolation coverage,
  invalid-tracks settings isolation coverage, unsupported-version settings
  isolation coverage, malformed-manifest recovery coverage,
  missing-manifest settings-isolation coverage, project and AppSession
  package-path file coverage, direct linked/broken package symlink rejection,
  linked/broken package-parent rejection, manifest directory rejection, manifest
  symlink/broken-symlink rejection, and successful save/load coverage.

## 4. Add AppSession Load Invalid Clips Schema Settings Isolation Coverage

Acceptance:
- Add focused `AppSession` coverage that loading a project package whose
  manifest has a track with a non-array `clips` field leaves an isolated app
  settings file unchanged.
- Verify session load reports a human-readable clips schema failure, keeps the
  current session project unchanged, leaves the manifest unchanged, leaves app
  settings loadable and byte-for-byte unchanged, and creates no
  `manifest.json.tmp`.
- Preserve clips schema rejection behavior, project invalid-clips schema
  settings isolation coverage, invalid-devices settings isolation coverage,
  loop-region value/schema settings isolation coverage, invalid-tracks settings
  isolation coverage, unsupported-version settings isolation coverage,
  malformed-manifest recovery coverage, missing-manifest settings-isolation
  coverage, project and AppSession package-path file coverage, direct
  linked/broken package symlink rejection, linked/broken package-parent
  rejection, manifest directory rejection, manifest symlink/broken-symlink
  rejection, and successful session save/load coverage.

## 5. Add Project Load Invalid Track Entry Settings Isolation Coverage

Acceptance:
- Add focused core coverage that loading a project package whose manifest has a
  non-object track entry leaves an isolated app settings file unchanged.
- Verify load reports a human-readable track-entry schema failure, leaves the
  manifest unchanged, leaves app settings loadable and byte-for-byte unchanged,
  and creates no `manifest.json.tmp`.
- Preserve track-entry schema rejection behavior, clips/devices settings
  isolation coverage, loop-region value/schema settings isolation coverage,
  invalid-tracks settings isolation coverage, unsupported-version settings
  isolation coverage, malformed-manifest recovery coverage,
  missing-manifest settings-isolation coverage, project and AppSession
  package-path file coverage, direct linked/broken package symlink rejection,
  linked/broken package-parent rejection, manifest directory rejection, manifest
  symlink/broken-symlink rejection, and successful save/load coverage.
