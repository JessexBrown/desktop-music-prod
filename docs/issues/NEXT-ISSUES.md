# Next Issues

## 1. Confirm GitHub-Hosted CI Run Status

Acceptance:
- Observe the pushed GitHub Actions workflow run once repository Actions status
  is accessible.
- Verify the `Windows MSVC App` job passes `projectname_app_smoke`.
- Verify the `Linux Core` job passes the `core-dev` configure/build/test path.
- If either job fails, fix the workflow while preserving FetchContent caching
  and second-host core coverage.

## 2. Add Package Media Restore Entry Selection Model

Acceptance:
- Add a plain C++ selection model for restorable entries in the selected cleanup
  batch.
- Support select all, clear selection, and toggle by original package-relative
  path.
- Keep existing restore execution unchanged until the UI can pass a selected
  path set deliberately.
- Cover restored, conflict, partial-failure, stale-path, and empty-selection
  states with unit tests.

## 3. Continue SPDX Baseline Reduction for App/Core Sources

Acceptance:
- Add SPDX headers to one small, reviewable group of legacy app or core source
  files.
- Remove those paths from `docs/SPDX_EXCEPTIONS.txt`.
- Keep `projectname_spdx_check` passing.

## 4. Add Save As Package Asset Copy Policy

Acceptance:
- Define whether Save As clones package-local audio, analysis, samples, presets,
  and backups or records a package relocation warning first.
- Keep external media references explicit and recoverable.
- Add tests or a documented UI test hook before copying package-local assets.
- Do not perform package copying on the audio callback thread.

## 5. Persist Audio/MIDI Setup Preferences

Acceptance:
- Define an application settings file location separate from project packages.
- Persist the first-run Audio/MIDI setup dismissal and preferred output intent.
- Keep device preference reads/writes off the audio callback thread.
- Add tests for settings serialization before applying preferences at startup.
