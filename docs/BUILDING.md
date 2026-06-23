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

See `docs/MACOS_CI_PREREQUISITES.md` for the macOS local baseline and the
future pinned GitHub Actions job shape.

The Windows `dev` preset configures the project in `out/build/dev-vs2026`; the
macOS/Linux `dev-host` preset configures in `out/build/dev`. Both enable tests
and fetch JUCE by default. Their CTest runs include the JUCE app launch smoke
test (`projectname --smoke-test`), hidden project chooser smoke test
(`projectname --smoke-project-choosers`), hidden Audio/MIDI reset smoke test
(`projectname --smoke-audio-midi-reset`), hidden app settings corruption
recovery smoke test (`projectname --smoke-app-settings-corruption`), hidden
restore-detail smoke test (`projectname --smoke-restore-details`), the SPDX
license-header check, the SPDX checker fixture test, the CI artifact contents
fixture test, the macOS artifact upload guard checks, and the core unit tests.

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
  On Windows with the `dev` preset, the built executable is
  `out/build/dev-vs2026/projectname_artefacts/Debug/Rabbington Studio.exe`.
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
  smoke test for New/Open/Save As success, cancellation, occupied-target Save
  As failure, final-manifest Save As failure with failed-target path copy, and
  failed Open states without opening native chooser dialogs.
- `projectname_audio_midi_reset_smoke` runs the hidden JUCE app settings-flow
  smoke test for clearing first-run Audio/MIDI dismissal and preferred output
  intent from an isolated temporary settings file.
- `projectname_app_settings_corruption_smoke` runs the hidden JUCE app
  settings-recovery smoke test for malformed and unsupported-version isolated
  `settings.json` fallback warning, valid rewrite, and unchanged active project
  package.
- `projectname_restore_detail_smoke` runs the hidden JUCE app Package
  Maintenance review-row smoke test for conflict/partial-failure restore
  manifests, Command/Ctrl+C package-relative path copy, and restore-manifest
  activation fallback/status copy without starting cleanup or restore jobs.
- `projectname_spdx_check` verifies first-party files either carry
  `SPDX-License-Identifier: AGPL-3.0-or-later` or are listed in
  `docs/SPDX_EXCEPTIONS.txt`, and currently enforces zero first-party baseline
  exceptions.
- `projectname_spdx_fixture_check` verifies the SPDX checker accepts
  `CMakePresets.json` vendor metadata for comment-free JSON, rejects missing
  first-party headers, rejects stale exception paths, and fails if a baseline
  exception appears while the production gate expects zero.
- `projectname_ci_artifact_contents_fixture_check` verifies the CI artifact
  allowlist checker accepts Windows and Linux app package shapes, rejects staged
  cache/plugin/preset/sample paths, and rejects checksum drift before upload.
- `projectname_ci_macos_artifact_guard_check` verifies the production CI
  workflow keeps the `macOS JUCE App` job build/test-only while preserving the
  existing Windows MSVC and Linux JUCE app upload steps.
- `projectname_ci_macos_artifact_guard_fixture_check` verifies the macOS
  artifact guard allows the current Windows/Linux upload shape, rejects an
  accidental macOS `upload-artifact` step, and fails if the app job names drift.
- `projectname_tests` runs the current unit tests.

## Continuous Integration

GitHub Actions currently runs four jobs on pushes and pull requests:

- `Windows MSVC App` configures, builds, and tests the `dev` preset on
  `windows-latest`. Its CTest run includes `projectname_app_smoke`,
  `projectname_project_chooser_smoke`, `projectname_audio_midi_reset_smoke`,
  `projectname_app_settings_corruption_smoke`,
  `projectname_restore_detail_smoke`, the SPDX check, the SPDX fixture check,
  the CI artifact contents fixture check, the macOS artifact guard checks, and
  the core unit tests. After tests pass, the job uploads a short-retention
  artifact named
  `rabbington-studio-windows-msvc-app-<commit-sha>` for 7 days.
- `Linux Core` configures, builds, and tests the `core-dev` preset on
  `ubuntu-latest` for dependency-light second-host coverage.
- `Linux JUCE App` installs the Ubuntu package baseline from
  `docs/LINUX_JUCE_APP_PREREQUISITES.md`, configures and builds `dev-host`, and
  runs `ctest --preset dev-host --output-on-failure` under `xvfb-run -a`.
  After tests pass, the job uploads a short-retention artifact named
  `rabbington-studio-linux-juce-app-<commit-sha>` for 7 days.
- `macOS JUCE App` pins `macos-15`, selects Xcode 16.4 explicitly, configures
  and builds `dev-host`, and runs
  `ctest --preset dev-host --output-on-failure`. It intentionally uploads no
  app artifact.

All CI jobs set `FETCHCONTENT_BASE_DIR` to a job-specific directory under
`.cache/fetchcontent` and cache that directory with `actions/cache`. This
reuses JUCE and nlohmann/json FetchContent downloads without committing
dependency sources, build trees, or generated artifacts. Keeping separate
cache directories for Windows MSVC, Linux Core, Linux JUCE App, and macOS JUCE
App builds helps avoid CMake subbuild generator collisions. The SPDX check
ignores local/generated roots such as `.cache/`, `out/`, and `build/` so
restored third-party dependency sources do not become part of the first-party
license-header baseline.
The Windows MSVC and Linux JUCE app artifacts are staged under each runner's
temporary directory and contain only the app executable plus `LICENSE`,
`README.md`, `docs/DEPENDENCIES.md`, an artifact note, and `SHA256SUMS.txt`.
Before upload, CI runs an exact allowlist check against the staged tree and
validates that `SHA256SUMS.txt` covers the expected files. The gate fails if
the package includes FetchContent caches, dependency checkouts, Visual
Studio/CMake build intermediates, test scratch directories, plugins, presets,
samples, proprietary assets, extra directories, symlinks, or checksum drift.
These CI artifacts are unsigned debug/smoke packages, not release installers.

### Downloading CI App Artifacts

For a successful `CI` workflow run, open the repository's GitHub Actions page,
choose the completed run, and use the `Artifacts` section on the run summary.
The app artifacts are only present after the matching app job has passed:

- Windows: `rabbington-studio-windows-msvc-app-<commit-sha>`
- Linux: `rabbington-studio-linux-juce-app-<commit-sha>`

GitHub keeps these CI artifacts for 7 days. The `<commit-sha>` suffix is the
full commit SHA for that workflow run. macOS CI is build/test-only and does not
upload an app artifact; any future macOS package, signing, notarization, or
installer implementation must follow
`docs/adr/ADR-0106-macos-artifact-signing-policy.md`.
The CTest guard `projectname_ci_macos_artifact_guard_check` intentionally fails
if `macOS JUCE App` adds `actions/upload-artifact` before that work lands. When
macOS artifact staging, checksum generation, unsigned debug messaging, signing,
notarization, or installer packaging is intentionally implemented, update or
retire `tools/check_ci_macos_artifact_upload_guard.cmake` and its fixture test
in the same reviewed change that implements the ADR-0106 requirements.

With GitHub CLI, find a successful run and download one or both app artifacts:

```powershell
gh run list --repo JessexBrown/desktop-music-prod --workflow CI --branch main --limit 5

$runId = '<run-id>'
$commitSha = '<commit-sha>'
$artifactRoot = Join-Path $env:TEMP 'rabbington-studio-artifacts'

gh run download $runId --repo JessexBrown/desktop-music-prod `
    --name "rabbington-studio-windows-msvc-app-$commitSha" `
    --dir (Join-Path $artifactRoot 'windows')

gh run download $runId --repo JessexBrown/desktop-music-prod `
    --name "rabbington-studio-linux-juce-app-$commitSha" `
    --dir (Join-Path $artifactRoot 'linux')
```

After extraction, the Windows artifact starts from its artifact root with:

```powershell
Start-Process ".\Rabbington Studio.exe"
```

The Linux artifact starts from its artifact root with:

```bash
chmod +x "./Rabbington Studio"
"./Rabbington Studio"
```

These downloaded artifacts are unsigned debug/smoke builds. They are useful for
manual inspection of the current app shell after CI has passed, but they do not
install Rabbington Studio, configure system audio dependencies, create desktop
shortcuts, sign/notarize binaries, or bundle proprietary plugins, presets,
samples, commercial sounds, or proprietary assets.

After downloading and extracting a CI app artifact, verify it from the extracted
artifact root. On Windows PowerShell:

```powershell
Get-Content .\SHA256SUMS.txt | ForEach-Object {
    $expected, $relativePath = $_ -split '\s+', 2
    $artifactPath = Join-Path (Get-Location) $relativePath
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifactPath).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "Checksum mismatch: $relativePath" }
}
```

On Linux:

```bash
sha256sum -c SHA256SUMS.txt
```

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
See `docs/MACOS_CI_PREREQUISITES.md` for the current macOS source review,
minimum JUCE/CMake assumptions, and build/test-only CI shape. See
`docs/adr/ADR-0106-macos-artifact-signing-policy.md` before adding any
downloadable macOS artifact, signing credentials, notarization, or installer.

### Linux

Install CMake, GCC or Clang, and the native development packages required by
JUCE for Linux desktop/audio builds, then use `dev-host` for the JUCE desktop app
build. See `docs/LINUX_JUCE_APP_PREREQUISITES.md` for the current Ubuntu package
baseline used by CI. ALSA/JACK/PipeWire policy and plugin scan paths will be
documented when device and plugin milestones expand.

## Current Local Verification Note

On June 22, 2026 in the current Codex workspace, CMake 4.4.0-rc1 was available
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
`projectname_project_chooser_smoke`, which exercises deterministic chooser
success, cancellation, duplicate New, occupied-target Save As failure, and
final-manifest Save As failure after a completed asset copy, and failed Open
states without opening native dialogs. The final-manifest failure path also
verifies the Project menu copy action for the kept target package path. The run
also passed
`projectname_audio_midi_reset_smoke`, which
seeds an isolated temporary settings file, dispatches the Audio/MIDI reset
command, and verifies persisted setup preferences are cleared without changing
the active project package, and `projectname_app_settings_corruption_smoke`,
which verifies malformed isolated app settings fall back to defaults and can be
surfaced as a Device Panel warning before reset rewrites valid human-readable
JSON without changing the active project package, and
`projectname_restore_detail_smoke`, which verifies Package
Maintenance review-row copy/activation behavior for conflict and partial-failure
restore manifests, including distinct package-relative and restore-manifest
fallback status copy, without starting cleanup or restore jobs. The same run also
passed `projectname_spdx_check`, `projectname_spdx_fixture_check`, and
`projectname_ci_artifact_contents_fixture_check`,
`projectname_ci_macos_artifact_guard_check`,
`projectname_ci_macos_artifact_guard_fixture_check`, plus `projectname_tests`,
including cached prepared voice-window playback, background voice-window
preparation, imported clip inspector metadata, deterministic imported clip
selection, and persisted track mix state/static mix command/sample-rate
mismatch coverage, plus app command registry metadata, enablement, and
dispatch-result coverage.

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
prepared voice summing tests, previous-manifest backup success/failure tests,
manifest-directory load failure/app-settings isolation tests, manifest symlink
and broken-symlink load failure/app-settings isolation tests when host-supported,
package-path file rejection tests, package directory creation failure tests,
package and asset-folder symlink path failure tests, including broken asset
folder links, when host-supported, staged temporary-manifest symlink cleanup
tests, including broken links, when host-supported, manifest symlink and
broken-symlink save rejection tests,
session-level manifest symlink and broken-symlink save failure tests when
host-supported, staged temporary-manifest open/commit failure tests, Save As retry manifest/asset
symlink conflict tests when host-supported, Save As source and target symlink
rejection tests, including broken source and target links, when host-supported,
background Save As broken source symlink failure tests when host-supported, plus
app settings load-directory, symlink, broken-symlink, empty-path,
save-symlink, save-broken-symlink, save-broken-parent-symlink,
temporary-symlink including broken links, path, and write-failure tests.
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
Audio/MIDI preferences without deleting or moving project packages. If the
settings file is malformed, unsupported, or a symlink, the app falls back to
defaults, surfaces the ignored-settings warning in the Device Panel, and clears
that warning after a successful settings rewrite. The hidden app reset and
corruption-recovery smoke tests verify the fallback/rewrite behavior against
temporary settings files, including malformed JSON and unsupported future
settings versions; core tests cover symlinked settings-file load/save rejection
and broken-symlink settings-file load/save rejection when host-supported.
Save As copies package-local `audio/`, `analysis/`, `samples/`, and `presets/`
on a cancellable background job before writing the target manifest, while
source-package `backups/` are not cloned. If the final manifest write fails
after copying, the active package remains unchanged and the copied target assets
remain in the chosen package for recovery or manual cleanup. The Project menu's
`Copy Failed Save As Target` action copies that kept target package path, and
the `Retry Failed Save As` action writes only the kept target manifest after the
blocking manifest path is fixed. Copy refuses symlinked source package folders,
source asset entries, target package paths, or intermediate target parents,
including broken target links, before mutating the target package. Retry refuses
existing target manifests, non-regular manifest paths, and missing or symlinked
copied target assets without starting cleanup, recopying assets, or touching the
audio callback. The
project chooser smoke test verifies the copy action plus retry conflict,
missing-asset, stale
temporary-manifest, and successful recovery paths without starting another Save
As job. Import Audio uses a
native WAV file chooser and a background import job with frame-level decode
progress, byte-level staged-copy progress, and cancel state.
The right inspector's selected-clip Relink button uses a native WAV chooser and
a cancellable background job for staged relink preparation before committing
current-selection results on the UI thread.
