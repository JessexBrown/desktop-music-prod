<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Restore Conflict Recovery Detail Actions

Acceptance:
- Add non-mutating detail actions for conflict and partial-failure restore
  entries, such as reveal manifest path or copy package-relative path.
- Do not overwrite active package media or re-run restore for review-blocked
  batches.
- Keep actions available from keyboard-focused Package Maintenance rows.
- Cover conflict and partial-failure action availability with core tests.

## 2. Continue SPDX Baseline Reduction for Root Config

Acceptance:
- Add SPDX headers to one small, reviewable group of remaining root/config
  exceptions.
- Remove those paths from `docs/SPDX_EXCEPTIONS.txt`.
- Keep `projectname_spdx_check` passing.
- Avoid changing document meaning or build behavior.

## 3. Add Project Chooser Failure-State Smoke Coverage

Acceptance:
- Extend hidden project chooser smoke coverage to cancelled selections, duplicate
  New targets, and failed Open attempts.
- Verify failures leave the current project package unchanged.
- Keep native chooser dialogs unavailable in automated tests.
- Preserve the existing launch and successful chooser smoke tests.

## 4. Add Audio/MIDI Reset App Smoke Coverage

Acceptance:
- Add a hidden app smoke hook that exercises the Audio/MIDI preference reset
  command against an isolated temporary settings file.
- Verify reset clears first-run dismissal and preferred output intent while
  leaving the active project package path unchanged.
- Keep the smoke hook unavailable in normal user workflows.
- Preserve existing launch and project chooser smoke tests.

## 5. Add Linux JUCE App CI Prerequisite Note

Acceptance:
- Document the Linux desktop/audio system packages expected before enabling a
  Linux JUCE app CI job.
- Keep the current `Linux Core` workflow job unchanged.
- Link the note from `docs/BUILDING.md`.
- Do not add or install packages in CI yet.
