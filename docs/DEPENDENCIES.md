<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Dependencies

Codex must update this file whenever adding, removing, or changing a dependency.

## Project License

Original Rabbington Studio source, documentation, and build scripts are licensed under
`AGPL-3.0-or-later`. Third-party dependencies retain their own licenses.

| Dependency | Version/Commit | Source | License | Used For | Notes |
|---|---:|---|---|---|---|
| nlohmann/json | 3.12.0 | https://github.com/nlohmann/json | MIT | Human-readable project manifest JSON serialization in the domain core | Header-only library fetched by CMake. Keeps model save/load testable without JUCE. |
| JUCE | 8.0.13 | https://github.com/juce-framework/JUCE | AGPLv3 or commercial JUCE license | Native app shell, audio/MIDI device management, GUI | Fetched by CMake from the official repository when `PROJECTNAME_BUILD_APP=ON`. The prototype assumes the AGPLv3 path unless maintainers choose a commercial JUCE license before distribution. |

## CI Actions

| Dependency | Version/Commit | Source | License | Used For | Notes |
|---|---:|---|---|---|---|
| actions/checkout | v7 | https://github.com/actions/checkout | MIT | GitHub Actions repository checkout | CI-only workflow action; not bundled with the app. Verified against release v7.0.0, whose action manifest declares the Node 24 runtime. |
| actions/cache | v5 | https://github.com/actions/cache | MIT | GitHub Actions FetchContent dependency cache | CI-only workflow action; caches `.cache/fetchcontent/*` directories without committing generated artifacts. Verified against release v5.0.5, whose action manifest declares the Node 24 runtime. |
| actions/upload-artifact | v7 | https://github.com/actions/upload-artifact | MIT | GitHub Actions app artifact upload | CI-only workflow action; uploads the staged Windows MSVC and Linux JUCE app packages after successful tests. Verified against release v7.0.1, whose action manifest declares the Node 24 runtime. |

Scheduled review checklist: `docs/CI_ACTION_PIN_REVIEW.md`.

## CI System Packages

| Dependency | Version/Commit | Source | License | Used For | Notes |
|---|---:|---|---|---|---|
| Ubuntu JUCE app build prerequisites | Ubuntu hosted-runner package versions | Ubuntu apt repositories | Distribution package licenses | Linux JUCE app CI configure/build/smoke-test support | Package names are listed in `docs/LINUX_JUCE_APP_PREREQUISITES.md`; packages are installed only on CI runners and are not bundled with Rabbington Studio. |

## Reviewed But Not Added

| Dependency | Version/Commit | Source | License | Why Not Added Yet |
|---|---:|---|---|---|
| Tracktion Engine | develop branch reviewed at `2877b621f2fbee564d0696a616b86bf8ba8c8ab0` | https://github.com/Tracktion/tracktion_engine | GPLv3-or-later or commercial Tracktion license | Useful DAW engine candidate, but not required for the first app shell/audio/project round trip. Needs a separate architecture and license decision before becoming a dependency. |

## Platform APIs

| API | Version | Source | License | Used For | Notes |
|---|---:|---|---|---|---|
| Win32/GDI/WinMM | Host Windows SDK/toolchain | Windows platform SDK or MinGW-w64 import libraries | Platform SDK/toolchain terms | Windows-only fallback verification app | No proprietary assets, plugins, samples, or redistributable SDK binaries are bundled. |

## Dependency Rules

- Do not add dependencies casually.
- Prefer maintained, cross-platform libraries.
- Prefer permissive or clearly compatible open-source licenses.
- Add exact versions or commits.
- Include build flags and platform constraints.
- Record binary assets separately from source dependencies.
- Never add sample packs, presets, fonts, icons, logos, or plugin binaries without license review.
