<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0003: Win32 Fallback Verification App

## Status

Accepted for the v0.1 prototype scaffold.

## Context

The intended product app remains a native cross-platform JUCE desktop
application. However, the current Windows workspace only has MinGW available, and
JUCE does not support MinGW. That prevented local verification of the requirements
that a desktop app launches and that generated audio can be sent to a device.

The project still needs a way to keep moving without adding broad unfinished
systems or changing the primary architecture away from JUCE.

## Decision

Add a small Windows-only `projectname_win32` fallback target behind
`PROJECTNAME_BUILD_WIN32_FALLBACK`.

The fallback target:
- uses plain Win32/GDI for a dark DAW layout skeleton;
- uses the existing `projectname_core` transport, project model, and audio engine
  stub;
- uses the Windows `waveOut` API to send a pre-rendered generated clip buffer to
  the default output device;
- supports `--smoke-test` for launch verification;
- supports `--smoke-audio` for generated-clip output verification.
- supports `--smoke-project` for app-level project package save/load
  verification.

It does not replace the JUCE app target, and it is not the long-term UI or audio
device architecture. The JUCE target remains the cross-platform app shell.

## Consequences

- Windows+MinGW can now configure, build, run tests, launch a native window,
  verify generated-clip output, and verify app-level project save/load.
- The fallback app has no new third-party dependency.
- The full JUCE app still requires MSVC or another JUCE-supported toolchain on
  Windows.
- Any feature beyond launch/audio smoke should go into the JUCE app path or the
  shared core unless explicitly scoped as fallback verification.
