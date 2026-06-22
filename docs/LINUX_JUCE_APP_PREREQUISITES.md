<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Linux JUCE App Prerequisites

This note records the Linux desktop/audio system packages used by the GitHub
Actions job that builds and smoke-tests the JUCE app on Linux. `Linux Core`
continues using the dependency-light `core-dev` preset as separate second-host
coverage.

## Scope

The current Rabbington Studio app target links JUCE modules for native
application/window integration, audio/MIDI device management, GUI widgets, and
painting:

- `juce_audio_devices`
- `juce_audio_utils`
- `juce_gui_extra`

The current target defines `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0`, does not
enable plugin hosting, and does not link `juce_opengl`. Package policy should be
revisited when those choices change.

## Ubuntu Package Baseline

For Ubuntu 24.04 or Ubuntu 22.04, start with this package set before running the
`dev-host` preset for the JUCE app:

```bash
sudo apt update
sudo apt install \
  build-essential \
  cmake \
  git \
  ninja-build \
  pkg-config \
  xvfb \
  libasound2-dev \
  libjack-jackd2-dev \
  libfreetype-dev \
  libfontconfig1-dev \
  libx11-dev \
  libxcomposite-dev \
  libxcursor-dev \
  libxext-dev \
  libxinerama-dev \
  libxrandr-dev \
  libxrender-dev
```

Notes:

- `pkg-config` is required because JUCE discovers several Linux system libraries
  through pkg-config.
- `xvfb` is expected for headless CI smoke tests, such as
  `xvfb-run -a ctest --preset dev-host --output-on-failure`.
- `libjack-jackd2-dev` provides the JACK development headers for JUCE's JACK
  backend. Installing the headers does not mean CI should start or require a
  JACK server.
- The smoke test should tolerate a missing physical audio device. It should
  prove app launch and safe setup behavior, not depend on CI runner audio
  hardware.
- If `libfreetype-dev` is unavailable on a target distribution, try
  `libfreetype6-dev`.

## Optional Packages Not In The Current Baseline

Do not install these in CI until the corresponding app feature or JUCE flag is
enabled:

- `libwebkit2gtk-4.1-dev` for Linux WebBrowserComponent support
  (`JUCE_WEB_BROWSER=1`).
- `libcurl4-openssl-dev` for JUCE curl-backed networking (`JUCE_USE_CURL=1`).
- `ladspa-sdk` when plugin hosting work links `juce_audio_processors` with
  LADSPA enabled.
- `libglu1-mesa-dev` and `mesa-common-dev` when the app links `juce_opengl`.

## CI Shape

The workflow uses a separate Linux app job rather than expanding `Linux Core` in
place:

```yaml
linux-juce-app:
  name: Linux JUCE App
  env:
    FETCHCONTENT_BASE_DIR: ${{ github.workspace }}/.cache/fetchcontent/linux-juce-app
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v7
    - name: Install JUCE app prerequisites
      run: sudo apt update && sudo apt install ...
    - name: Configure app
      run: cmake --preset dev-host -DFETCHCONTENT_BASE_DIR="${FETCHCONTENT_BASE_DIR}"
    - name: Build app
      run: cmake --build --preset dev-host
    - name: Test app
      run: xvfb-run -a ctest --preset dev-host --output-on-failure
```

`Linux JUCE App` uses its own FetchContent cache directory and key so CMake
subbuilds cannot collide with `Linux Core` or Windows MSVC caches.

## Source

This package baseline is derived from JUCE 8.0.13's
`docs/Linux Dependencies.md`, adjusted for Rabbington Studio's current compile
definitions and linked modules.
