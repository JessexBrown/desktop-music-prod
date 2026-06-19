# Next Issues

## 1. Continue SPDX Baseline Reduction for App Shell Sources

Acceptance:
- Add SPDX headers to the remaining legacy app shell source files:
  `src/app/Main.cpp`, `src/app/MainComponent.*`, and
  `src/platform/win32/Win32FallbackApp.cpp`.
- Remove those paths from `docs/SPDX_EXCEPTIONS.txt`.
- Keep `projectname_spdx_check` passing.

## 2. Add Save As Package Asset Copy Policy

Acceptance:
- Define whether Save As clones package-local audio, analysis, samples, presets,
  and backups or records a package relocation warning first.
- Keep external media references explicit and recoverable.
- Add tests or a documented UI test hook before copying package-local assets.
- Do not perform package copying on the audio callback thread.

## 3. Persist Audio/MIDI Setup Preferences

Acceptance:
- Define an application settings file location separate from project packages.
- Persist the first-run Audio/MIDI setup dismissal and preferred output intent.
- Keep device preference reads/writes off the audio callback thread.
- Add tests for settings serialization before applying preferences at startup.

## 4. Refresh GitHub Actions Node Runtime Usage

Acceptance:
- Review the hosted CI annotation about Node.js 20 actions being forced to
  Node.js 24.
- Update GitHub Actions pins or add a documented follow-up if upstream actions
  have not released Node 24-native versions yet.
- Preserve the existing Linux core and Windows app build/test coverage.
- Verify the workflow still passes on GitHub-hosted runners.

## 5. Add Restore-Selection Keyboard Affordances

Acceptance:
- Add keyboard-accessible commands for select all, clear selection, and toggling
  the focused restore entry without requiring pointer input.
- Keep batch navigation predictable when restore-entry rows are present.
- Preserve restore disabled states for empty selection, package-busy,
  conflict-review, and partial-failure-review batches.
- Cover command availability and state transitions with core or app-session
  tests.
