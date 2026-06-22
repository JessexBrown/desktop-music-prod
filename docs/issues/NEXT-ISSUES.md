<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Project Chooser Failure-State Smoke Coverage

Acceptance:
- Extend hidden project chooser smoke coverage to cancelled selections, duplicate
  New targets, and failed Open attempts.
- Verify failures leave the current project package unchanged.
- Keep native chooser dialogs unavailable in automated tests.
- Preserve the existing launch and successful chooser smoke tests.

## 2. Add Audio/MIDI Reset App Smoke Coverage

Acceptance:
- Add a hidden app smoke hook that exercises the Audio/MIDI preference reset
  command against an isolated temporary settings file.
- Verify reset clears first-run dismissal and preferred output intent while
  leaving the active project package path unchanged.
- Keep the smoke hook unavailable in normal user workflows.
- Preserve existing launch and project chooser smoke tests.

## 3. Add Linux JUCE App CI Prerequisite Note

Acceptance:
- Document the Linux desktop/audio system packages expected before enabling a
  Linux JUCE app CI job.
- Keep the current `Linux Core` workflow job unchanged.
- Link the note from `docs/BUILDING.md`.
- Do not add or install packages in CI yet.

## 4. Add Restore Manifest Reveal App Wiring

Acceptance:
- Wire the restore detail model's manifest-path action to a platform-aware
  reveal action or a copy-manifest-path fallback.
- Keep the action non-mutating and unavailable for rows without conflict or
  partial-failure review details.
- Cover the focused-row action mapping with a small app or core test.
- Do not re-run restore or touch package media from the reveal/copy path.

## 5. Resolve CMake Presets SPDX Exception

Acceptance:
- Decide whether `CMakePresets.json` should keep a reviewed SPDX exception or
  use an allowed CMake presets metadata field for the SPDX identifier.
- Verify `cmake --preset dev` still parses the presets file after any change.
- Keep `projectname_spdx_check` passing.
- Update `docs/SOURCE_HEADER_POLICY.md` if JSON configuration files need a
  documented exception rule.
