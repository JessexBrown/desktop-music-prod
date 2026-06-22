<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Resolve CMake Presets SPDX Exception

Acceptance:
- Decide whether `CMakePresets.json` should keep a reviewed SPDX exception or
  use an allowed CMake presets metadata field for the SPDX identifier.
- Verify `cmake --preset dev` still parses the presets file after any change.
- Keep `projectname_spdx_check` passing.
- Update `docs/SOURCE_HEADER_POLICY.md` if JSON configuration files need a
  documented exception rule.

## 2. Add Save As Failure App Smoke Coverage

Acceptance:
- Extend hidden project chooser smoke coverage to a deterministic Save As
  package-copy failure, such as a target nested inside the source package or an
  occupied target conflict.
- Verify failure leaves the current project package and manifest unchanged.
- Ensure the background Save As job is cleaned up and the Cancel Save affordance
  is disabled after the failure result is applied.
- Preserve the existing successful, cancelled, duplicate New, and failed Open
  chooser smoke coverage.

## 3. Add App Settings Corruption Recovery Smoke Coverage

Acceptance:
- Add a hidden app smoke hook that points `MainComponent` at an isolated
  malformed `settings.json`.
- Verify the app falls back to default app settings without mutating the active
  project package path.
- Verify a follow-up settings save or reset writes a valid human-readable
  settings file at the isolated path.
- Preserve the existing launch, project chooser, and Audio/MIDI reset smoke
  tests.

## 4. Add Linux JUCE App CI Job

Acceptance:
- Add a separate `Linux JUCE App` workflow job that installs the package baseline
  from `docs/LINUX_JUCE_APP_PREREQUISITES.md`.
- Keep the existing `Linux Core` job unchanged.
- Configure and build `dev-host`, then run `ctest --preset dev-host
  --output-on-failure` under `xvfb-run -a`.
- Use a Linux-app-specific FetchContent cache directory and key so it does not
  collide with Windows MSVC or Linux core caches.

## 5. Add Restore Detail App Smoke Coverage

Acceptance:
- Add a hidden app smoke hook that builds an isolated package with a
  conflict/partial-failure restore manifest and focuses a review row.
- Verify Command/Ctrl+C copies the package-relative original path.
- Verify review-row activation uses the manifest reveal/copy fallback path
  without starting restore or cleanup jobs.
- Preserve existing launch, project chooser, and Audio/MIDI reset smoke tests.
