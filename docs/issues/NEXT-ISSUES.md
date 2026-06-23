<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Save Backup Failure Coverage

Acceptance:
- Add focused core coverage for a project save failure while creating
  `backups/manifest.previous.json`.
- Verify the active `manifest.json` remains unchanged when backup creation
  fails.
- Verify the staged `manifest.json.tmp`, if created, is removed or explicitly
  documented if the current writer cannot clean it safely.
- Keep the fixture deterministic on Windows, macOS, and Linux.

## 2. Add App Settings Temporary Write Failure Coverage

Acceptance:
- Add focused core coverage for an app settings save failure before
  `settings.json.tmp` can be opened or written.
- Verify the existing `settings.json`, when present, remains unchanged.
- Verify the occupied temporary path remains unchanged and the error is
  human-readable.
- Preserve the successful settings save/load, reset, and commit-failure tests.

## 3. Add App Settings Directory Creation Failure Coverage

Acceptance:
- Add focused core coverage for an app settings save failure when the configured
  settings parent directory path is occupied by a file.
- Verify no `settings.json.tmp` is created in the blocked path.
- Verify the occupied parent path remains unchanged and the error is
  human-readable.
- Preserve the successful settings save/load, reset, commit-failure, and
  temporary-write-failure tests.

## 4. Add macOS Build-Test CI Job

Acceptance:
- Add a `macOS JUCE App` GitHub Actions job using the
  `docs/MACOS_CI_PREREQUISITES.md` assumptions.
- Pin the runner to `macos-15`, select Xcode 16.4 explicitly, use the existing
  `dev-host` preset, and give the job a dedicated FetchContent cache path.
- Run configure, build, and `ctest --preset dev-host --output-on-failure`.
- Do not upload a macOS artifact, sign, notarize, create an installer, or bundle
  plugins, presets, samples, commercial sounds, or proprietary assets.

## 5. Add Save As Failed Target Retry Command

Acceptance:
- Add `project.saveAs.retryFailedTargetManifest` using ADR-0104's enablement,
  manifest-only retry, and overwrite/conflict rules.
- Expose it near `Copy Failed Save As Target` without deleting, quarantining, or
  recopying target package assets.
- Cover successful retry after the blocking manifest path is removed, existing
  target `manifest.json` conflict, missing copied target assets, and stale
  `manifest.json.tmp` behavior.
- Verify retry does not start a Save As package-copy job and does not touch the
  real-time audio path.
