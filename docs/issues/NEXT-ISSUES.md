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

## 5. Design Package-Local Media Quarantine and Restore Commands

Acceptance:
- Create an ADR for a package-local quarantine folder and restore manifest
  format that can support undoable media cleanup later.
- Define move-to-quarantine transaction, rollback, and restore expectations for
  inventory candidates without adding permanent deletion.
- Specify how restoration should interact with current manifests, previous
  manifest backups, missing-media placeholders, and active package work.
- Keep actual deletion/restoration implementation, command-palette entries, and
  drag/drop clip operations out of this task.
