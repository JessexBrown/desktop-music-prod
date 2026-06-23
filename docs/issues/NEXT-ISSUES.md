<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add macOS CI Prerequisites Note

Acceptance:
- Document the expected macOS JUCE app build prerequisites and GitHub Actions
  runner/toolchain assumptions before adding a macOS app CI job.
- Decide whether macOS CI should be added immediately or deferred until
  signing/package policy is clearer, and record any meaningful decision in ADR.
- Preserve the current local `dev-host` build path and avoid signing,
  notarization, installer, bundled-plugin, preset, sample, or proprietary-asset
  claims.

## 2. Add Save As Failed Target Retry Design

Acceptance:
- Define the UX and command boundary for retrying a target manifest save after a
  post-copy Save As manifest failure.
- Preserve the current policy that copied target assets are not automatically
  deleted or quarantined.
- Document overwrite/conflict behavior before adding a visible retry command.
- Keep retry planning and any future file operations off the real-time audio
  path.

## 3. Add Project Save Backup Failure Coverage

Acceptance:
- Add focused core coverage for a project save failure while creating
  `backups/manifest.previous.json`.
- Verify the active `manifest.json` remains unchanged when backup creation
  fails.
- Verify the staged `manifest.json.tmp`, if created, is removed or explicitly
  documented if the current writer cannot clean it safely.
- Keep the fixture deterministic on Windows, macOS, and Linux.

## 4. Add App Settings Temporary Write Failure Coverage

Acceptance:
- Add focused core coverage for an app settings save failure before
  `settings.json.tmp` can be opened or written.
- Verify the existing `settings.json`, when present, remains unchanged.
- Verify the occupied temporary path remains unchanged and the error is
  human-readable.
- Preserve the successful settings save/load, reset, and commit-failure tests.

## 5. Add App Settings Directory Creation Failure Coverage

Acceptance:
- Add focused core coverage for an app settings save failure when the configured
  settings parent directory path is occupied by a file.
- Verify no `settings.json.tmp` is created in the blocked path.
- Verify the occupied parent path remains unchanged and the error is
  human-readable.
- Preserve the successful settings save/load, reset, commit-failure, and
  temporary-write-failure tests.
