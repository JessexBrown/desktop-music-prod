<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Linux JUCE App CI Prerequisite Note

Acceptance:
- Document the Linux desktop/audio system packages expected before enabling a
  Linux JUCE app CI job.
- Keep the current `Linux Core` workflow job unchanged.
- Link the note from `docs/BUILDING.md`.
- Do not add or install packages in CI yet.

## 2. Add Restore Manifest Reveal App Wiring

Acceptance:
- Wire the restore detail model's manifest-path action to a platform-aware
  reveal action or a copy-manifest-path fallback.
- Keep the action non-mutating and unavailable for rows without conflict or
  partial-failure review details.
- Cover the focused-row action mapping with a small app or core test.
- Do not re-run restore or touch package media from the reveal/copy path.

## 3. Resolve CMake Presets SPDX Exception

Acceptance:
- Decide whether `CMakePresets.json` should keep a reviewed SPDX exception or
  use an allowed CMake presets metadata field for the SPDX identifier.
- Verify `cmake --preset dev` still parses the presets file after any change.
- Keep `projectname_spdx_check` passing.
- Update `docs/SOURCE_HEADER_POLICY.md` if JSON configuration files need a
  documented exception rule.

## 4. Add Save As Failure App Smoke Coverage

Acceptance:
- Extend hidden project chooser smoke coverage to a deterministic Save As
  package-copy failure, such as a target nested inside the source package or an
  occupied target conflict.
- Verify failure leaves the current project package and manifest unchanged.
- Ensure the background Save As job is cleaned up and the Cancel Save affordance
  is disabled after the failure result is applied.
- Preserve the existing successful, cancelled, duplicate New, and failed Open
  chooser smoke coverage.

## 5. Add App Settings Corruption Recovery Smoke Coverage

Acceptance:
- Add a hidden app smoke hook that points `MainComponent` at an isolated
  malformed `settings.json`.
- Verify the app falls back to default app settings without mutating the active
  project package path.
- Verify a follow-up settings save or reset writes a valid human-readable
  settings file at the isolated path.
- Preserve the existing launch, project chooser, and Audio/MIDI reset smoke
  tests.
