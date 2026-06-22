<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Save As Manifest Write Failure Smoke Coverage

Acceptance:
- Add a hidden project chooser smoke path for a target where package asset copy
  succeeds or is unnecessary but final manifest save fails deterministically.
- Verify the app does not switch the active project package when the final
  manifest write fails.
- Verify copied package assets are left in a clearly recoverable state or a
  follow-up cleanup policy is documented.
- Preserve the existing successful, cancelled, occupied-target failure,
  duplicate New, and failed Open chooser smoke coverage.

## 2. Add App Settings Load Warning Surface

Acceptance:
- Preserve the current safe fallback for malformed app settings.
- Record the settings load error in app state so the Device Panel or status
  label can tell users preferences were ignored.
- Keep the warning out of the real-time audio path.
- Cover the visible warning with a focused unit or smoke assertion.

## 3. Add Linux JUCE App CI Artifact Upload

Acceptance:
- Upload the Linux JUCE app executable or packaged build directory as a CI
  artifact only after the `Linux JUCE App` job builds and tests successfully.
- Keep the artifact retention short and documented.
- Do not include FetchContent source caches or build-system intermediates in the
  artifact.
- Preserve the existing Windows MSVC App, Linux Core, and Linux JUCE App test
  behavior.

## 4. Add Restore Detail Status Copy Coverage

Acceptance:
- Add focused coverage that the Package Maintenance status line distinguishes
  package-relative path copy from restore-manifest fallback copy.
- Keep the coverage deterministic and avoid opening platform file browsers.
- Preserve the existing restore-detail smoke path and core row-model tests.

## 5. Add Windows MSVC App CI Artifact Upload

Acceptance:
- Upload the Windows app executable or packaged build directory as a CI artifact
  only after the `Windows MSVC App` job builds and tests successfully.
- Keep the artifact retention short and documented.
- Do not include FetchContent source caches, Visual Studio intermediates, or
  test scratch directories in the artifact.
- Preserve the existing Windows MSVC App, Linux Core, and Linux JUCE App test
  behavior.
