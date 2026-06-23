<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Manifest Load Directory Failure Coverage

Acceptance:
- Add focused core coverage for loading a project package whose `manifest.json`
  path is occupied by a directory.
- Verify load fails with a human-readable missing/unreadable manifest error and
  does not mutate an existing `AppSession` project when routed through session
  loading.
- Preserve malformed JSON, unsupported-version, schema, and missing-manifest
  load failure coverage.

## 2. Add Project Save Package Directory Creation Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package when the package
  directory cannot be created because an intermediate parent path is occupied by
  a regular file.
- Verify the occupied parent file remains unchanged and no package asset folders
  or manifest temp files are created.
- Verify the error is human-readable.
- Preserve package-path file rejection, asset-folder failure, temporary-manifest
  failure, backup failure, commit failure, and successful save/load tests.

## 3. Add App Settings Load Directory Path Coverage

Acceptance:
- Add focused core coverage for loading app settings when the settings path is
  occupied by a directory.
- Verify load returns no settings without parsing JSON or mutating caller-owned
  default settings.
- Verify the app fallback path remains explicit in tests when no persisted
  settings are loaded.
- Preserve missing-file, malformed JSON, unsupported-version, reset, and
  successful load/save coverage.

## 4. Add macOS Artifact Upload Guardrail Check

Acceptance:
- Add a lightweight repository check that verifies the `macOS JUCE App` CI job
  remains build/test-only until ADR-0106 artifact rules are implemented.
- Verify the check allows the existing Windows MSVC and Linux JUCE app artifact
  uploads while rejecting an accidental macOS `upload-artifact` step.
- Document how to update or retire the check when macOS artifact staging,
  checksum generation, unsigned debug messaging, signing, notarization, or
  installer work is intentionally added.
- Preserve the existing CI job names and current Windows/Linux artifact tests.

## 5. Add Save As Retry Asset Symlink Rejection Coverage

Acceptance:
- Add focused retry preflight coverage for copied package asset paths that are
  symlinks instead of regular files.
- Fixture-gate the symlink case cleanly when the host cannot create test
  symlinks without elevated permissions.
- Verify the asset symlink target is not overwritten or removed, retry does not
  recopy assets or write a manifest, and the error is human-readable.
- Preserve the existing regular-file, directory, missing-asset,
  manifest-symlink, and stale temporary-manifest retry coverage.
