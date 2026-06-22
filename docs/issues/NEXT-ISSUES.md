<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Unsupported App Settings Version Smoke Coverage

Acceptance:
- Seed the hidden app settings corruption smoke path with a future
  `settingsVersion` fixture in addition to malformed JSON.
- Verify the app falls back to defaults, surfaces the ignored-settings warning,
  and clears it after reset.
- Preserve the existing malformed settings recovery smoke behavior.
- Keep all settings recovery work outside the real-time audio path.

## 2. Add CI Artifact Checksums

Acceptance:
- Generate a SHA-256 checksum file for each uploaded app artifact.
- Include the checksum file inside the staged artifact package.
- Document how to verify the checksum after downloading an artifact.
- Preserve the existing short retention and exclusion of dependency caches,
  build intermediates, test scratch data, plugins, presets, samples, and
  proprietary assets.

## 3. Add CI Artifact Download Instructions

Acceptance:
- Document where to find the Windows MSVC and Linux JUCE app artifacts for a
  successful GitHub Actions run.
- Include the expected artifact names, 7-day retention, and launch caveats for
  unsigned debug/smoke packages.
- Keep the docs clear that CI artifacts are not release installers.
- Preserve the existing no-proprietary-plugins, no-presets, no-samples, and
  no-proprietary-assets claims.

## 4. Add Save As Failed Target Reveal Action

Acceptance:
- After a post-copy Save As manifest failure, expose a non-destructive way to
  reveal or copy the kept target package path.
- Preserve the policy that copied target assets are not automatically deleted or
  quarantined.
- Add focused app smoke coverage for the reveal/copy action and status copy.
- Keep all target package file operations off the real-time audio path.

## 5. Add Project Save Permission Failure Coverage

Acceptance:
- Add focused core coverage for a project save failure before manifest commit,
  such as an unwritable package directory or asset folder creation failure.
- Verify no stale `manifest.json.tmp` remains when the failure happens after the
  temporary manifest path is known.
- Verify the existing manifest and previous-manifest backup behavior remain
  unchanged for successful saves.
- Keep the fixture deterministic on Windows, macOS, and Linux.
