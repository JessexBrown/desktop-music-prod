<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Restore Detail App Smoke Coverage

Acceptance:
- Add a hidden app smoke hook that builds an isolated package with a
  conflict/partial-failure restore manifest and focuses a review row.
- Verify Command/Ctrl+C copies the package-relative original path.
- Verify review-row activation uses the manifest reveal/copy fallback path
  without starting restore or cleanup jobs.
- Preserve existing launch, project chooser, and Audio/MIDI reset smoke tests.

## 2. Add Source Header Check Fixtures

Acceptance:
- Add a small CTest-friendly fixture path that proves the SPDX checker accepts
  CMake presets vendor metadata for JSON files without comments.
- Verify the fixture keeps stale exceptions and missing-header failures covered.
- Keep the production `projectname_spdx_check` target passing with zero
  first-party exceptions.

## 3. Add Save As Manifest Write Failure Smoke Coverage

Acceptance:
- Add a hidden project chooser smoke path for a target where package asset copy
  succeeds or is unnecessary but final manifest save fails deterministically.
- Verify the app does not switch the active project package when the final
  manifest write fails.
- Verify copied package assets are left in a clearly recoverable state or a
  follow-up cleanup policy is documented.
- Preserve the existing successful, cancelled, occupied-target failure,
  duplicate New, and failed Open chooser smoke coverage.

## 4. Add App Settings Load Warning Surface

Acceptance:
- Preserve the current safe fallback for malformed app settings.
- Record the settings load error in app state so the Device Panel or status
  label can tell users preferences were ignored.
- Keep the warning out of the real-time audio path.
- Cover the visible warning with a focused unit or smoke assertion.

## 5. Add Linux JUCE App CI Artifact Upload

Acceptance:
- Upload the Linux JUCE app executable or packaged build directory as a CI
  artifact only after the `Linux JUCE App` job builds and tests successfully.
- Keep the artifact retention short and documented.
- Do not include FetchContent source caches or build-system intermediates in the
  artifact.
- Preserve the existing Windows MSVC App, Linux Core, and Linux JUCE App test
  behavior.
