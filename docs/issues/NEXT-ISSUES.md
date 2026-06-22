<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Linux JUCE App CI Artifact Upload

Acceptance:
- Upload the Linux JUCE app executable or packaged build directory as a CI
  artifact only after the `Linux JUCE App` job builds and tests successfully.
- Keep the artifact retention short and documented.
- Do not include FetchContent source caches or build-system intermediates in the
  artifact.
- Preserve the existing Windows MSVC App, Linux Core, and Linux JUCE App test
  behavior.

## 2. Add Windows MSVC App CI Artifact Upload

Acceptance:
- Upload the Windows app executable or packaged build directory as a CI artifact
  only after the `Windows MSVC App` job builds and tests successfully.
- Keep the artifact retention short and documented.
- Do not include FetchContent source caches, Visual Studio intermediates, or
  test scratch directories in the artifact.
- Preserve the existing Windows MSVC App, Linux Core, and Linux JUCE App test
  behavior.

## 3. Add Save As Failed Target Cleanup Policy

Acceptance:
- Decide whether post-copy Save As manifest failures should keep, quarantine, or
  offer deletion for copied target assets.
- Record the cleanup/recovery policy in an ADR and user-facing status copy.
- Add focused smoke or core coverage for the chosen cleanup/recovery path.
- Preserve the current guarantee that the active project package is not switched
  when the final Save As manifest write fails.

## 4. Add Project Manifest Commit Failure Core Coverage

Acceptance:
- Add a core save-package test that forces the staged `manifest.json.tmp` commit
  to fail deterministically.
- Verify the failed save removes `manifest.json.tmp`.
- Verify the occupied manifest path remains unchanged and the error is
  human-readable.
- Preserve the existing successful save, backup, and load round-trip tests.

## 5. Add Unsupported App Settings Version Smoke Coverage

Acceptance:
- Seed the hidden app settings corruption smoke path with a future
  `settingsVersion` fixture in addition to malformed JSON.
- Verify the app falls back to defaults, surfaces the ignored-settings warning,
  and clears it after reset.
- Preserve the existing malformed settings recovery smoke behavior.
- Keep all settings recovery work outside the real-time audio path.
