<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# macOS CI Prerequisites

This note records the expected macOS desktop/audio build assumptions before a
GitHub Actions macOS app job is added. It intentionally does not add signing,
notarization, installer creation, app artifact upload, plugin hosting, bundled
plugins, presets, samples, or proprietary assets.

## Scope

The current Rabbington Studio app target links JUCE modules for native
application/window integration, audio/MIDI device management, GUI widgets, and
painting:

- `juce_audio_devices`
- `juce_audio_utils`
- `juce_gui_extra`

The current target defines `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0`, does not
enable plugin hosting, and does not link `juce_opengl`. macOS CI policy should
be revisited when those choices change, especially when Audio Unit hosting,
plugin scanning, or installer/signing work begins.

## Local macOS Baseline

For local macOS app builds, install:

- Xcode 12.4 or newer. JUCE 8.0.13 lists Xcode 12.4 as the minimum for building
  JUCE projects on macOS/iOS.
- CMake 3.24 or newer. JUCE 8.0.13 requires CMake 3.22 or newer, while this
  repository's presets require CMake 3.24.
- Git, available to CMake configure because FetchContent retrieves
  nlohmann/json 3.12.0 and JUCE 8.0.13.

Use the existing host-generator preset:

```bash
cmake --preset dev-host
cmake --build --preset dev-host
ctest --preset dev-host --output-on-failure
```

The `dev-host` preset configures in `out/build/dev`, enables tests, and fetches
JUCE by default. This is the same local path used on Linux and should remain the
documented macOS path until a macOS-specific preset is justified.

## GitHub Actions Runner Assumptions

The macOS app CI job is build/test-only and pins the runner image explicitly
instead of using `macos-latest`.

Current job shape:

```yaml
macos-juce-app:
  name: macOS JUCE App
  runs-on: macos-15
  env:
    FETCHCONTENT_BASE_DIR: ${{ github.workspace }}/.cache/fetchcontent/macos-juce-app
  steps:
    - uses: actions/checkout@v7
    - name: Select Xcode 16.4
      run: |
        sudo xcode-select -s /Applications/Xcode_16.4.app/Contents/Developer
        xcodebuild -version
    - name: Cache FetchContent downloads
      uses: actions/cache@v5
      with:
        path: .cache/fetchcontent/macos-juce-app
        key: macos-juce-app-fetchcontent-${{ hashFiles('CMakeLists.txt', 'CMakePresets.json') }}
        restore-keys: |
          macos-juce-app-fetchcontent-
    - name: Configure app
      run: cmake --preset dev-host -DFETCHCONTENT_BASE_DIR="${FETCHCONTENT_BASE_DIR}"
    - name: Build app
      run: cmake --build --preset dev-host
    - name: Test app
      run: ctest --preset dev-host --output-on-failure
```

Notes:

- Pin `macos-15` for the first job because `macos-latest` is moving to
  `macos-26` during June-July 2026, which also changes the default Xcode from
  16.4 to 26.4.1.
- Select `/Applications/Xcode_16.4.app` explicitly so the job does not silently
  change compilers if the runner image default changes.
- Use a dedicated `.cache/fetchcontent/macos-juce-app` cache directory and key
  so CMake FetchContent subbuilds cannot collide with Windows MSVC, Linux Core,
  or Linux JUCE App caches.
- Do not upload a macOS artifact from the first job. Verify configure, build,
  and smoke tests first; follow
  `docs/adr/ADR-0106-macos-artifact-signing-policy.md` before adding artifact
  upload, signing/notarization, installer work, or unsigned-debug-package
  messaging.
- The smoke tests should tolerate missing physical audio devices. They should
  prove app launch and safe setup behavior, not depend on runner audio hardware.
- If a standard `macos-15` arm64 runner exposes a JUCE-specific issue, use the
  explicit `macos-15-intel` label as a separate investigation rather than
  switching to `macos-latest`.

## Not In The Current Baseline

Do not add these to macOS CI until a separate task implements the policy in
`docs/adr/ADR-0106-macos-artifact-signing-policy.md`:

- Apple Developer certificates, provisioning profiles, signing identities, or
  notarization credentials.
- Installer, DMG, pkg, zip, or appcast generation.
- Uploading a macOS `.app` bundle as a downloadable artifact.
- Audio Unit, VST3, CLAP, LV2, AAX, or other plugin scanning/hosting tests.
- Bundled third-party plugins, presets, samples, commercial sounds, logos, or
  proprietary assets.
- Automatic installation of third-party plugin binaries.

## Source Review

Last verified: 2026-06-23.

- JUCE 8.0.13 README:
  `https://raw.githubusercontent.com/juce-framework/JUCE/8.0.13/README.md`
- GitHub-hosted runners reference:
  `https://docs.github.com/en/actions/reference/runners/github-hosted-runners`
- GitHub Actions macOS 15 runner image:
  `https://raw.githubusercontent.com/actions/runner-images/main/images/macos/macos-15-Readme.md`
- GitHub Actions `macos-latest` migration announcement:
  `https://github.com/actions/runner-images/issues/14167`
- GitHub Actions Xcode signing guide:
  `https://docs.github.com/en/actions/how-tos/deploy/deploy-to-third-party-platforms/sign-xcode-applications`
