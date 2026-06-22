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

On June 19, 2026, refresh the GitHub-maintained action pins from
`actions/checkout@v4` and `actions/cache@v4` to `actions/checkout@v7` and
`actions/cache@v5`. GitHub release metadata showed `checkout` v7.0.0 and
`cache` v5.0.5 available, and both action manifests declare the Node 24 runtime.
This removes the hosted-runner annotation about Node.js 20 actions being forced
onto Node.js 24 without changing the CI job topology.

Update the SPDX checker to ignore local/generated roots such as `.cache/`,
`out/`, and `build/`. Restored third-party dependency sources should not be
treated as first-party files for license-header enforcement.

On June 22, 2026, document Linux JUCE app CI prerequisites in
`docs/LINUX_JUCE_APP_PREREQUISITES.md` without changing the workflow. The note
uses JUCE 8.0.13's Linux dependency guidance, adjusted for Rabbington Studio's
current `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, no-plugin-host, and no-OpenGL
app target. Linux desktop app CI remains a separate future job.

Later on June 22, 2026, add that separate `Linux JUCE App` workflow job. It
installs the documented Ubuntu package baseline, uses the `dev-host` preset to
configure and build the JUCE app, and runs `ctest --preset dev-host
--output-on-failure` under `xvfb-run -a`. The existing `Linux Core` job remains
unchanged. The Linux app job uses `.cache/fetchcontent/linux-juce-app` and a
`linux-juce-app-fetchcontent-*` cache key so its CMake FetchContent subbuilds
cannot collide with Windows MSVC or Linux Core caches.

Later on June 22, 2026, add a Linux JUCE app CI artifact upload. The workflow
stages a small package under the runner temporary directory only after
`ctest --preset dev-host` passes. The staged artifact contains the app
executable, `LICENSE`, `README.md`, `docs/DEPENDENCIES.md`, and an artifact
note, then uploads it with `actions/upload-artifact@v7` for 7 days. The package
must not include FetchContent caches, dependency checkouts, CMake build-system
intermediates, test scratch data, plugins, presets, samples, or proprietary
assets.

Later on June 22, 2026, add the matching Windows MSVC app CI artifact upload.
The workflow stages a small package under the Windows runner temporary
directory only after `ctest --preset dev --output-on-failure` passes. The staged
artifact contains `Rabbington Studio.exe`, `LICENSE`, `README.md`,
`docs/DEPENDENCIES.md`, and an artifact note, then uploads it with
`actions/upload-artifact@v7` for 7 days. The package must not include
FetchContent caches, dependency checkouts, Visual Studio/CMake build-system
intermediates, test scratch data, plugins, presets, samples, or proprietary
assets. It is an unsigned CI debug package, not a release installer.

Later on June 22, 2026, generate `SHA256SUMS.txt` inside each staged Windows
MSVC and Linux JUCE app artifact before upload. The checksum file is created
after all package files are copied and excludes only `SHA256SUMS.txt` itself,
so a downloaded artifact can be verified without expanding the artifact scope or
adding another CI dependency.

## Consequences

- Windows CI continues to verify the desktop app launch smoke test.
- The core model, serialization, timing, audio-rendering, package-maintenance,
  and command-routing tests now run on a second host OS.
- Dependency downloads are reused across CI runs without committing generated
  artifacts.
- CI uses GitHub-maintained Node 24 action runtimes for checkout and caching.
- SPDX enforcement remains focused on first-party files even when CI restores a
  dependency cache into the workspace.
- Linux JUCE app CI now verifies the native JUCE target on Ubuntu in addition
  to Windows MSVC app coverage and Linux core-only coverage.
- Linux JUCE app CI now produces a short-lived executable artifact for manual
  smoke inspection without turning generated build trees or dependency caches
  into downloadable artifacts.
- Windows MSVC app CI now produces the same kind of short-lived executable
  artifact after the existing Windows build/test gate passes.
- Downloaded CI app artifacts can be integrity-checked against the checksum file
  included in the staged package.

## Follow-Ups

- Add a README CI status badge after the public branch protection/check naming
  settles.
