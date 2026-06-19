# Next Issues

## 1. Wire Package Media Restore Selection Into Maintenance UI

Acceptance:
- Surface restorable cleanup-batch entries as selectable browser rows or a
  focused detail model.
- Support select all, clear selection, and toggle-entry commands from app/UI
  state.
- Disable restore when no entries are selected, when package work is active, or
  when conflict/partial-failure review blocks execution.
- Pass selected original package-relative paths into the existing background
  restore request only after the user has made a deliberate selection.
- Cover empty, selected, select-all, conflict, and package-busy states with
  core or app-session tests.

## 2. Continue SPDX Baseline Reduction for App/Core Sources

Acceptance:
- Add SPDX headers to one small, reviewable group of legacy app or core source
  files.
- Remove those paths from `docs/SPDX_EXCEPTIONS.txt`.
- Keep `projectname_spdx_check` passing.

## 3. Add Save As Package Asset Copy Policy

Acceptance:
- Define whether Save As clones package-local audio, analysis, samples, presets,
  and backups or records a package relocation warning first.
- Keep external media references explicit and recoverable.
- Add tests or a documented UI test hook before copying package-local assets.
- Do not perform package copying on the audio callback thread.

## 4. Persist Audio/MIDI Setup Preferences

Acceptance:
- Define an application settings file location separate from project packages.
- Persist the first-run Audio/MIDI setup dismissal and preferred output intent.
- Keep device preference reads/writes off the audio callback thread.
- Add tests for settings serialization before applying preferences at startup.

## 5. Refresh GitHub Actions Node Runtime Usage

Acceptance:
- Review the hosted CI annotation about Node.js 20 actions being forced to
  Node.js 24.
- Update GitHub Actions pins or add a documented follow-up if upstream actions
  have not released Node 24-native versions yet.
- Preserve the existing Linux core and Windows app build/test coverage.
- Verify the workflow still passes on GitHub-hosted runners.
