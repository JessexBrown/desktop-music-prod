<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add App Settings Corruption Recovery Smoke Coverage

Acceptance:
- Add a hidden app smoke hook that points `MainComponent` at an isolated
  malformed `settings.json`.
- Verify the app falls back to default app settings without mutating the active
  project package path.
- Verify a follow-up settings save or reset writes a valid human-readable
  settings file at the isolated path.
- Preserve the existing launch, project chooser, and Audio/MIDI reset smoke
  tests.

## 2. Add Linux JUCE App CI Job

Acceptance:
- Add a separate `Linux JUCE App` workflow job that installs the package baseline
  from `docs/LINUX_JUCE_APP_PREREQUISITES.md`.
- Keep the existing `Linux Core` job unchanged.
- Configure and build `dev-host`, then run `ctest --preset dev-host
  --output-on-failure` under `xvfb-run -a`.
- Use a Linux-app-specific FetchContent cache directory and key so it does not
  collide with Windows MSVC or Linux core caches.

## 3. Add Restore Detail App Smoke Coverage

Acceptance:
- Add a hidden app smoke hook that builds an isolated package with a
  conflict/partial-failure restore manifest and focuses a review row.
- Verify Command/Ctrl+C copies the package-relative original path.
- Verify review-row activation uses the manifest reveal/copy fallback path
  without starting restore or cleanup jobs.
- Preserve existing launch, project chooser, and Audio/MIDI reset smoke tests.

## 4. Add Source Header Check Fixtures

Acceptance:
- Add a small CTest-friendly fixture path that proves the SPDX checker accepts
  CMake presets vendor metadata for JSON files without comments.
- Verify the fixture keeps stale exceptions and missing-header failures covered.
- Keep the production `projectname_spdx_check` target passing with zero
  first-party exceptions.

## 5. Add Save As Manifest Write Failure Smoke Coverage

Acceptance:
- Add a hidden project chooser smoke path for a target where package asset copy
  succeeds or is unnecessary but final manifest save fails deterministically.
- Verify the app does not switch the active project package when the final
  manifest write fails.
- Verify copied package assets are left in a clearly recoverable state or a
  follow-up cleanup policy is documented.
- Preserve the existing successful, cancelled, occupied-target failure,
  duplicate New, and failed Open chooser smoke coverage.
