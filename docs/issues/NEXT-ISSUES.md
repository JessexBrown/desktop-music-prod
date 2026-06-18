# Next Issues

## 1. Reduce SPDX Baseline Exceptions

Acceptance:
- Add SPDX headers to one small, reviewable group of existing first-party files.
- Remove those paths from `docs/SPDX_EXCEPTIONS.txt`.
- Keep `projectname_spdx_check` passing.

## 2. Harden CI Caching and Smoke Coverage

Acceptance:
- Cache JUCE/nlohmann FetchContent downloads where appropriate without committing
  generated artifacts.
- Confirm `projectname_app_smoke` runs reliably on Windows CI with MSVC.
- Add a core-only CI job for a second host OS before expanding app packaging.

## 3. Replace Deterministic Save/Open With Native Project Choosers

Acceptance:
- Add native file chooser actions for New, Save As, and Open.
- Keep the deterministic `ProjectName Demo.project` path available for smoke
  tests or replace it with an equally deterministic test hook.
- Save the current transport/project model to a chosen `.project` package.
- Load a chosen package and refresh visible project metadata.
- Surface missing or invalid manifest errors in the app UI without replacing the
  current project.

## 4. Design the Audio/MIDI First-Run Setup Flow

Acceptance:
- Create a short UX note for first-run device setup.
- Define success/error states for unavailable audio devices.
- Add a non-modal path back to the Audio/MIDI setup dialog.
- Keep the first implementation separate from plugin scanning.

## 5. Add Compact Timeline Pan Controls

Acceptance:
- Add small visible controls for timeline pan left/right near the existing
  workspace viewport indicator.
- Route controls through existing session viewport pan methods and refresh the
  lane plus indicator after each command.
- Keep existing focused keyboard pan/zoom behavior and reset/zoom controls
  intact.
- Do not add clip editing, drag/drop behavior, or global shortcuts.
