<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0088: CI FetchContent Cache and Core Host Coverage

## Status

Accepted.

## Context

Rabbington Studio needs CI to prove that the native desktop app can configure,
build, launch in smoke-test mode, and run unit tests. The first CI job covered
the Windows MSVC app path, but each clean run re-downloaded FetchContent
dependencies and there was no second-host coverage for the plain C++ core.

Linux desktop app CI requires additional JUCE system package policy and is
better handled later, before packaging expands.

## Decision

Keep the Windows MSVC CI job as the app-smoke gate. It runs the `dev` preset and
therefore covers:

- configuring the JUCE desktop app with MSVC;
- building the app and core tests;
- running `projectname_app_smoke`, `projectname_spdx_check`, and
  `projectname_tests`.

Add a Linux `core-dev` CI job for second-host coverage without building the JUCE
app. Both CI jobs set `FETCHCONTENT_BASE_DIR` to a job-specific directory under
`.cache/fetchcontent` and use `actions/cache` for that directory. The cache
stores FetchContent downloads for JUCE and/or nlohmann/json, while build outputs
stay in the CMake binary directories and remain untracked. Separate cache
directories prevent CMake FetchContent subbuild generator collisions between
Windows MSVC and Linux core jobs.

Update the SPDX checker to ignore local/generated roots such as `.cache/`,
`out/`, and `build/`. Restored third-party dependency sources should not be
treated as first-party files for license-header enforcement.

## Consequences

- Windows CI continues to verify the desktop app launch smoke test.
- The core model, serialization, timing, audio-rendering, package-maintenance,
  and command-routing tests now run on a second host OS.
- Dependency downloads are reused across CI runs without committing generated
  artifacts.
- SPDX enforcement remains focused on first-party files even when CI restores a
  dependency cache into the workspace.
- Linux JUCE app CI remains a future task once its system package requirements
  are documented.

## Follow-Ups

- Add a README CI status badge after the public branch protection/check naming
  settles.
- Add Linux JUCE app CI after documenting required desktop/audio development
  packages.
