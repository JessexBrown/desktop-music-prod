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
- Keep the deterministic `Rabbington Studio Demo.project` path available for smoke
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

## 5. Add Package Media Cleanup Background Job

Acceptance:
- Add a cancellable background job wrapper for package media inventory,
  quarantine preflight, quarantine move, and restore commands.
- Report phase/progress and final command status without running file moves on
  the UI thread or audio callback.
- Cover successful quarantine, successful restore, cancellation before start,
  active package-work rejection, and command failure propagation with tests.
- Keep visible cleanup UI, permanent deletion, and retention policy out of this
  task.
