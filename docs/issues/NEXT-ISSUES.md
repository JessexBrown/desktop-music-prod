<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add CI Artifact Checksums

Acceptance:
- Generate a SHA-256 checksum file for each uploaded app artifact.
- Include the checksum file inside the staged artifact package.
- Document how to verify the checksum after downloading an artifact.
- Preserve the existing short retention and exclusion of dependency caches,
  build intermediates, test scratch data, plugins, presets, samples, and
  proprietary assets.

## 2. Add CI Artifact Download Instructions

Acceptance:
- Document where to find the Windows MSVC and Linux JUCE app artifacts for a
  successful GitHub Actions run.
- Include the expected artifact names, 7-day retention, and launch caveats for
  unsigned debug/smoke packages.
- Keep the docs clear that CI artifacts are not release installers.
- Preserve the existing no-proprietary-plugins, no-presets, no-samples, and
  no-proprietary-assets claims.

## 3. Add Save As Failed Target Reveal Action

Acceptance:
- After a post-copy Save As manifest failure, expose a non-destructive way to
  reveal or copy the kept target package path.
- Preserve the policy that copied target assets are not automatically deleted or
  quarantined.
- Add focused app smoke coverage for the reveal/copy action and status copy.
- Keep all target package file operations off the real-time audio path.

## 4. Add Project Save Permission Failure Coverage

Acceptance:
- Add focused core coverage for a project save failure before manifest commit,
  such as an unwritable package directory or asset folder creation failure.
- Verify no stale `manifest.json.tmp` remains when the failure happens after the
  temporary manifest path is known.
- Verify the existing manifest and previous-manifest backup behavior remain
  unchanged for successful saves.
- Keep the fixture deterministic on Windows, macOS, and Linux.

## 5. Add App Settings Save Commit Failure Coverage

Acceptance:
- Add focused core coverage for an app settings save failure when the final
  `settings.json.tmp` commit cannot replace `settings.json`.
- Verify the failed save removes `settings.json.tmp`.
- Verify the occupied settings path remains unchanged and the error is
  human-readable.
- Preserve the existing successful settings save/load and reset tests.
