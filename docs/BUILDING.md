<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Building

## Expected Tooling

- CMake 3.24 or newer.
- A C++20 compiler.
- A native build system supported by CMake, such as Ninja, Visual Studio, Xcode,
  or Unix Makefiles.
- Git access during configure, because CMake fetches nlohmann/json 3.12.0 and,
  when `PROJECTNAME_BUILD_APP=ON`, JUCE 8.0.13 from GitHub.
- Platform SDKs as needed.

## Preferred Commands

On Windows, install Visual Studio 2026 or Visual Studio 2026 Build Tools with
the Desktop development with C++ workload before configuring the JUCE app. The
`dev` preset intentionally selects the Visual Studio 2026 generator, MSVC,
and x64 so a normal PowerShell does not accidentally reuse MinGW for the JUCE
desktop target:

```powershell
cmake --preset dev --fresh
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

On macOS and Linux, use the host-generator developer preset:

```bash
cmake --preset dev-host
cmake --build --preset dev-host
ctest --preset dev-host --output-on-failure
```

The Windows `dev` preset configures the project in `out/build/dev-vs2026`; the
macOS/Linux `dev-host` preset configures in `out/build/dev`. Both enable tests
and fetch JUCE by default. Their CTest runs include the JUCE app launch smoke
test (`projectname --smoke-test`), the SPDX license-header check, and the core
unit tests.

For domain-only model/transport/tone tests without building the JUCE app:

```bash
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev --output-on-failure
```

On Windows machines that only have MinGW, pass the generator explicitly:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --preset core-dev -G 'MinGW Makefiles'
& 'C:\Program Files\CMake\bin\cmake.exe' --build --preset core-dev
& 'C:\Program Files\CMake\bin\ctest.exe' --preset core-dev --output-on-failure
```

For the Windows-only fallback launcher used when a machine has MinGW but not a
JUCE-supported compiler:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --preset win32-fallback -G 'MinGW Makefiles'
& 'C:\Program Files\CMake\bin\cmake.exe' --build --preset win32-fallback
& 'C:\Program Files\CMake\bin\ctest.exe' --preset win32-fallback --output-on-failure
Start-Process -FilePath '.\out\build\win32-fallback\projectname_win32.exe' -ArgumentList '--smoke-test' -Wait -PassThru
Start-Process -FilePath '.\out\build\win32-fallback\projectname_win32.exe' -ArgumentList '--smoke-audio' -Wait -PassThru
Start-Process -FilePath '.\out\build\win32-fallback\projectname_win32.exe' -ArgumentList '--smoke-project' -Wait -PassThru
Start-Process -FilePath '.\out\build\win32-fallback\projectname_win32.exe' -ArgumentList '--smoke-import' -Wait -PassThru
```

To use a locally installed JUCE package instead of fetching it:

```bash
cmake --preset dev -DPROJECTNAME_FETCH_JUCE=OFF -DJUCE_DIR=/path/to/JUCE/cmake
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

## Targets

- `projectname` launches the native desktop app shell.
- `projectname_core` contains the app session, transport, project model, audio
  engine stub, tone rendering code, PCM16 WAV importer, project-package audio
  import service, background audio import job, workspace command router, and app
  command registry/dispatcher.
- `projectname_win32` launches a Windows-only fallback verification app when
  `PROJECTNAME_BUILD_WIN32_FALLBACK=ON`.
- `projectname_win32_import_smoke` verifies PCM16 WAV project-package import and
  prepared-buffer rendering through the fallback target.
- `projectname_app_smoke` runs the desktop app launch smoke test when
  `PROJECTNAME_BUILD_APP=ON` and `BUILD_TESTING=ON`.
- `projectname_project_chooser_smoke` runs the hidden JUCE app chooser-flow
  smoke test for New/Open/Save As without opening native chooser dialogs.
- `projectname_spdx_check` verifies first-party files either carry
  `SPDX-License-Identifier: AGPL-3.0-or-later` or are listed in
  `docs/SPDX_EXCEPTIONS.txt`.
- `projectname_tests` runs the current unit tests.

## Continuous Integration

GitHub Actions currently runs two jobs on pushes and pull requests:

- `Windows MSVC App` configures, builds, and tests the `dev` preset on
  `windows-latest`. Its CTest run includes `projectname_app_smoke`,
  `projectname_project_chooser_smoke`, the SPDX check, and the core unit tests.
- `Linux Core` configures, builds, and tests the `core-dev` preset on
  `ubuntu-latest` for second-host coverage before Linux app packaging work is
  ready.

Both jobs set `FETCHCONTENT_BASE_DIR` to a job-specific directory under
`.cache/fetchcontent` and cache that directory with `actions/cache`. This
reuses JUCE and nlohmann/json FetchContent downloads without committing
dependency sources, build trees, or generated artifacts. Keeping separate
cache directories for Windows MSVC and Linux core builds avoids CMake subbuild
generator collisions. The SPDX check ignores local/generated roots such as
`.cache/`, `out/`, and `build/` so restored third-party dependency sources do
not become part of the first-party license-header baseline.

Review GitHub-maintained action major pins with
`docs/CI_ACTION_PIN_REVIEW.md`; pin reviews should keep the existing workflow
jobs and FetchContent cache behavior stable unless a separate task says
otherwise.

## Platform Notes

### Windows

Install:

- CMake 3.24 or newer. If `cmake` is not on `PATH`, use the full path, such as
  `& 'C:\Program Files\CMake\bin\cmake.exe'`.
- Visual Studio 2026 or Visual Studio 2026 Build Tools.
- The Visual Studio workload `Desktop development with C++`.
- These Visual Studio components:
  - MSVC C++ x64/x86 build tools.
  - Windows 10 or Windows 11 SDK.
  - C++ CMake tools for Windows.

The Windows `dev` preset uses `Visual Studio 18 2026`, `x64`, and the
`out/build/dev-vs2026` binary directory. This avoids stale-generator collisions
with older MinGW or Visual Studio 2022 build trees.

JUCE does not support MinGW for this app scaffold. Use MSVC via Visual Studio or
another JUCE-supported Windows compiler toolchain.

ASIO is not enabled or bundled in this first slice. The app uses JUCE device
management and whatever supported system devices are available. Plugin scan paths
are intentionally not implemented yet.

### macOS

Install Xcode or Command Line Tools plus CMake, then use `dev-host` for the JUCE
desktop app build. Audio Unit hosting is not implemented in this first slice.
Signing/notarization are release tasks, not part of the local prototype build.

### Linux

Install CMake, GCC or Clang, and the native development packages required by
JUCE for Linux desktop/audio builds, then use `dev-host` for the JUCE desktop app
build. ALSA/JACK/PipeWire policy and plugin scan paths will be documented when
device and plugin milestones expand.

## Current Local Verification Note

On June 18, 2026 in the current Codex workspace, CMake 4.4.0-rc1 was available
at `C:\Program Files\CMake\bin\cmake.exe`. Visual Studio Community 2026
18.7.1 was installed at `C:\Program Files\Microsoft Visual Studio\18\Community`,
and CMake detected MSVC 19.51.36248.0 with the `Visual Studio 18 2026`
generator.

The Windows `dev` preset successfully configured in `out/build/dev-vs2026`,
built the JUCE desktop app, and ran:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --preset dev --fresh
& 'C:\Program Files\CMake\bin\cmake.exe' --build --preset dev
& 'C:\Program Files\CMake\bin\ctest.exe' --preset dev --output-on-failure
```

The `dev` CTest run passed `projectname_app_smoke`, which launches the JUCE app
with `--smoke-test` and exits automatically after startup, plus
`projectname_spdx_check` and `projectname_tests`, including cached prepared
voice-window playback, background voice-window preparation, imported clip
inspector metadata, deterministic imported clip selection, and persisted track
mix state/static mix command/sample-rate mismatch coverage, plus app command
registry metadata, enablement, and dispatch-result coverage.

The MinGW toolchain also successfully configured, built, and ran the `core-dev`
test suite, including generated-tone and generated-clip audio engine tests,
scheduled generated-clip tests, prepared mono buffer clip tests, PCM16 WAV
import tests, project-package audio import tests, background audio import
success/failure/cancel tests, waveform summary/thumbnail loading tests,
waveform regeneration tests, timeline clip lane layout and placement tests,
timeline clip hit-testing tests, loop-region persistence/session advance/render
layout tests, app-session import handoff tests, imported clip beat-to-sample
playback planning/rendering tests,
imported timeline Play preparation/cache tests, device-chain placeholder
manifest tests, background timeline playback preparation tests, prepared-cache
hit/miss/eviction/stale rejection, byte-budget, and invalidation tests,
background timeline preparation cancelled/stale completion tests, background
voice-window preparation tests, track voice scheduling tests, cached prepared
voice-window playback tests, imported clip inspector metadata tests,
deterministic imported clip selection and keyboard traversal tests, sample-rate
mismatch metadata propagation tests, timeline viewport state/command tests,
timeline fit-to-clips and selected-clip centering helper tests, imported clip
placement and media replacement undo/redo tests, imported clip edit undo/redo
command routing tests, imported clip media relink preparation/background job
tests, imported media package inventory tests, package media quarantine
restore-manifest/preflight/file-moving/restore/background-job tests, workspace command routing tests, app command
registry metadata/enablement/dispatch-result tests, timeline viewport indicator
formatting tests, persisted track mix state/static mix command tests, stereo
prepared voice summing tests, and previous-manifest backup tests.
It also
configured, built, launched, audio-smoke-tested,
project-save/load-smoke-tested, and audio-import-smoke-tested the Windows-only
`projectname_win32` fallback target.

JUCE does not support MinGW for the desktop app target; CMake still guards that
combination. Use the `dev` preset for the JUCE desktop app on Windows and the
`core-dev` or `win32-fallback` presets for dependency-light verification.

When the JUCE app is built, it starts from a deterministic package named
`Rabbington Studio Demo.project` in the current user's documents folder, and the
Project menu can create, open, save, or Save As native `.project` packages.
Application-level settings, including Audio/MIDI setup prompt dismissal and the
preferred output intent, are stored separately under JUCE's per-user application
data location as `Rabbington Studio/settings.json`; these settings are not part
of project packages. The Device Panel's `Reset Prefs` action clears those saved
Audio/MIDI preferences without deleting or moving project packages.
Save As copies package-local `audio/`, `analysis/`, `samples/`, and `presets/`
on a cancellable background job before writing the target manifest, while
source-package `backups/` are not cloned. Import Audio uses a native WAV file
chooser and a background import job with frame-level decode progress, byte-level
staged-copy progress, and cancel state.
The right inspector's selected-clip Relink button uses a native WAV chooser and
a cancellable background job for staged relink preparation before committing
current-selection results on the UI thread.
