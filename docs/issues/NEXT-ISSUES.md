<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add macOS Artifact Upload Guardrail Check

Acceptance:
- Add a lightweight repository check that verifies the `macOS JUCE App` CI job
  remains build/test-only until ADR-0106 artifact rules are implemented.
- Verify the check allows the existing Windows MSVC and Linux JUCE app artifact
  uploads while rejecting an accidental macOS `upload-artifact` step.
- Document how to update or retire the check when macOS artifact staging,
  checksum generation, unsigned debug messaging, signing, notarization, or
  installer work is intentionally added.
- Preserve the existing CI job names and current Windows/Linux artifact tests.

## 2. Add Save As Retry Asset Symlink Rejection Coverage

Acceptance:
- Add focused retry preflight coverage for copied package asset paths that are
  symlinks instead of regular files.
- Fixture-gate the symlink case cleanly when the host cannot create test
  symlinks without elevated permissions.
- Verify the asset symlink target is not overwritten or removed, retry does not
  recopy assets or write a manifest, and the error is human-readable.
- Preserve the existing regular-file, directory, missing-asset,
  manifest-symlink, and stale temporary-manifest retry coverage.

## 3. Add Project Manifest Load Symlink Failure Coverage

Acceptance:
- Add focused core coverage for loading a project package whose `manifest.json`
  path is a symlink.
- Fixture-gate the symlink case cleanly when the host cannot create test
  symlinks without elevated permissions.
- Verify load fails without parsing JSON through the symlink, leaves the link
  target untouched, and does not mutate an existing `AppSession` project.
- Preserve missing-manifest, manifest-directory, malformed JSON,
  unsupported-version, schema, and successful load/save coverage.

## 4. Add Project Save Package Symlink Path Failure Coverage

Acceptance:
- Add focused core coverage for saving a project package when the target package
  path or an intermediate parent path is a symlink.
- Fixture-gate the symlink case cleanly when the host cannot create test
  symlinks without elevated permissions.
- Verify save does not follow the symlink, does not create package asset folders
  or manifest temp files, and leaves the symlink target untouched.
- Preserve package-path file rejection, package-directory-creation failure,
  asset-folder failure, temporary-manifest failure, backup failure, commit
  failure, and successful save/load tests.

## 5. Add App Settings Load Symlink Path Coverage

Acceptance:
- Add focused core coverage for loading app settings when the settings path is a
  symlink.
- Fixture-gate cleanly when the host cannot create test symlinks without
  elevated permissions.
- Verify load returns no settings without parsing JSON through the symlink,
  leaves the link target untouched, and keeps caller fallback/default settings
  unchanged.
- Preserve missing-file, load-directory, malformed JSON, unsupported-version,
  reset, and successful load/save coverage.
