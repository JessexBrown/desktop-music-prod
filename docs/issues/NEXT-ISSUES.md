<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Next Issues

## 1. Add Audio/MIDI Preference Reset Action

Acceptance:
- Add a visible app command to reset saved Audio/MIDI preferences without
  deleting project packages.
- Clear the persisted first-run dismissal and preferred output intent from the
  app settings file.
- Keep reset reads/writes off the audio callback thread.
- Cover reset behavior with core settings tests and app command metadata tests.

## 2. Add CI Action Pin Review Note

Acceptance:
- Add a short scheduled-maintenance note or issue template for reviewing
  GitHub-maintained action major pins.
- Include the primary source to check for runtime changes: each action's release
  page and `action.yml` manifest.
- Keep this as a documentation/process task, not an automated updater.
- Preserve the existing CI workflow jobs and cache behavior.

## 3. Add Restore Conflict Recovery Detail Actions

Acceptance:
- Add non-mutating detail actions for conflict and partial-failure restore
  entries, such as reveal manifest path or copy package-relative path.
- Do not overwrite active package media or re-run restore for review-blocked
  batches.
- Keep actions available from keyboard-focused Package Maintenance rows.
- Cover conflict and partial-failure action availability with core tests.

## 4. Continue SPDX Baseline Reduction for Root Config

Acceptance:
- Add SPDX headers to one small, reviewable group of remaining root/config
  exceptions.
- Remove those paths from `docs/SPDX_EXCEPTIONS.txt`.
- Keep `projectname_spdx_check` passing.
- Avoid changing document meaning or build behavior.

## 5. Add Project Chooser Failure-State Smoke Coverage

Acceptance:
- Extend hidden project chooser smoke coverage to cancelled selections, duplicate
  New targets, and failed Open attempts.
- Verify failures leave the current project package unchanged.
- Keep native chooser dialogs unavailable in automated tests.
- Preserve the existing launch and successful chooser smoke tests.
