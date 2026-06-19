# Next Issues

## 1. Refresh GitHub Actions Node Runtime Usage

Acceptance:
- Review the hosted CI annotation about Node.js 20 actions being forced to
  Node.js 24.
- Update GitHub Actions pins or add a documented follow-up if upstream actions
  have not released Node 24-native versions yet.
- Preserve the existing Linux core and Windows app build/test coverage.
- Verify the workflow still passes on GitHub-hosted runners.

## 2. Add Restore-Selection Keyboard Affordances

Acceptance:
- Add keyboard-accessible commands for select all, clear selection, and toggling
  the focused restore entry without requiring pointer input.
- Keep batch navigation predictable when restore-entry rows are present.
- Preserve restore disabled states for empty selection, package-busy,
  conflict-review, and partial-failure-review batches.
- Cover command availability and state transitions with core or app-session
  tests.

## 3. Continue SPDX Baseline Reduction for Docs/Config

Acceptance:
- Add SPDX headers to one small, reviewable group of legacy docs or config files.
- Remove those paths from `docs/SPDX_EXCEPTIONS.txt`.
- Keep `projectname_spdx_check` passing.

## 4. Add Project Chooser Smoke Test Hooks

Acceptance:
- Add a deterministic test or smoke hook for New/Open/Save As flows without
  requiring an interactive native chooser.
- Cover Save As no-copy and package-asset-copy success paths through the app
  boundary.
- Keep the hook unavailable in normal user workflows.
- Preserve the existing `projectname_app_smoke` launch check.

## 5. Add Audio/MIDI Preference Reset Action

Acceptance:
- Add a visible app command to reset saved Audio/MIDI preferences without
  deleting project packages.
- Clear the persisted first-run dismissal and preferred output intent from the
  app settings file.
- Keep reset reads/writes off the audio callback thread.
- Cover reset behavior with core settings tests and app command metadata tests.
