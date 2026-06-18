# Next Issues

## 1. Confirm GitHub-Hosted CI Run Status

Acceptance:
- Observe the pushed GitHub Actions workflow run once repository Actions status
  is accessible.
- Verify the `Windows MSVC App` job passes `projectname_app_smoke`.
- Verify the `Linux Core` job passes the `core-dev` configure/build/test path.
- If either job fails, fix the workflow while preserving FetchContent caching
  and second-host core coverage.

## 2. Replace Deterministic Save/Open With Native Project Choosers

Acceptance:
- Add native file chooser actions for New, Save As, and Open.
- Keep the deterministic `Rabbington Studio Demo.project` path available for smoke
  tests or replace it with an equally deterministic test hook.
- Save the current transport/project model to a chosen `.project` package.
- Load a chosen package and refresh visible project metadata.
- Surface missing or invalid manifest errors in the app UI without replacing the
  current project.

## 3. Design the Audio/MIDI First-Run Setup Flow

Acceptance:
- Create a short UX note for first-run device setup.
- Define success/error states for unavailable audio devices.
- Add a non-modal path back to the Audio/MIDI setup dialog.
- Keep the first implementation separate from plugin scanning.

## 4. Add Package Media Restore Entry Selection Model

Acceptance:
- Add a plain C++ selection model for restorable entries in the selected cleanup
  batch.
- Support select all, clear selection, and toggle by original package-relative
  path.
- Keep existing restore execution unchanged until the UI can pass a selected
  path set deliberately.
- Cover restored, conflict, partial-failure, stale-path, and empty-selection
  states with unit tests.

## 5. Continue SPDX Baseline Reduction for App/Core Sources

Acceptance:
- Add SPDX headers to one small, reviewable group of legacy app or core source
  files.
- Remove those paths from `docs/SPDX_EXCEPTIONS.txt`.
- Keep `projectname_spdx_check` passing.
