<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Save As Failed Target Reveal Action

Acceptance:
- After a post-copy Save As manifest failure, expose a non-destructive way to
  reveal or copy the kept target package path.
- Preserve the policy that copied target assets are not automatically deleted or
  quarantined.
- Add focused app smoke coverage for the reveal/copy action and status copy.
- Keep all target package file operations off the real-time audio path.

## 2. Add Project Save Permission Failure Coverage

Acceptance:
- Add focused core coverage for a project save failure before manifest commit,
  such as an unwritable package directory or asset folder creation failure.
- Verify no stale `manifest.json.tmp` remains when the failure happens after the
  temporary manifest path is known.
- Verify the existing manifest and previous-manifest backup behavior remain
  unchanged for successful saves.
- Keep the fixture deterministic on Windows, macOS, and Linux.

## 3. Add App Settings Save Commit Failure Coverage

Acceptance:
- Add focused core coverage for an app settings save failure when the final
  `settings.json.tmp` commit cannot replace `settings.json`.
- Verify the failed save removes `settings.json.tmp`.
- Verify the occupied settings path remains unchanged and the error is
  human-readable.
- Preserve the existing successful settings save/load and reset tests.

## 4. Add CI Artifact Package Contents Gate

Acceptance:
- Add a CI-side check that the staged Windows MSVC and Linux JUCE app artifacts
  contain only the expected executable, `LICENSE`, `README.md`,
  `docs/DEPENDENCIES.md`, `ARTIFACT.txt`, and `SHA256SUMS.txt`.
- Fail the workflow if dependency caches, build intermediates, test scratch
  data, plugins, presets, samples, or proprietary assets are staged.
- Preserve the current 7-day artifact retention and checksum verification flow.

## 5. Add macOS CI Prerequisites Note

Acceptance:
- Document the expected macOS JUCE app build prerequisites and GitHub Actions
  runner/toolchain assumptions before adding a macOS app CI job.
- Decide whether macOS CI should be added immediately or deferred until
  signing/package policy is clearer, and record any meaningful decision in ADR.
- Preserve the current local `dev-host` build path and avoid signing,
  notarization, installer, bundled-plugin, preset, sample, or proprietary-asset
  claims.
