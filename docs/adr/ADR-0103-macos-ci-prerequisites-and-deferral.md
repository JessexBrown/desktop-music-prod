<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0103: macOS CI Prerequisites and Deferral

## Status

Accepted; build/test follow-up implemented by ADR-0105.

## Context

Rabbington Studio needs macOS coverage for the cross-platform native DAW goal,
but the current CI workflow only builds the JUCE app on Windows and Linux. The
repository already uses the `dev-host` preset for non-Windows local app builds,
so macOS should not need a separate local build path unless the host generator
or app-bundle behavior demands it later.

The macOS runner/toolchain surface is currently moving. GitHub's
`macos-latest` label is migrating from macOS 15 to macOS 26 during June-July
2026, with a default Xcode change from 16.4 to 26.4.1. The current app target
does not need macOS signing, notarization, installer creation, plugin hosting,
or downloadable `.app` artifact packaging to prove the v0.1 vertical slice.

## Decision

Add `docs/MACOS_CI_PREREQUISITES.md` to document the macOS build assumptions,
official source review, and future GitHub Actions job shape. ADR-0105 later
implements that build/test-only job.

Defer adding a macOS GitHub Actions app job in this slice. Once the documented
runner/toolchain assumptions have been reviewed, the next macOS CI step should
be a small build/test-only job that:

- uses the existing `dev-host` preset;
- pins `runs-on: macos-15` rather than `macos-latest`;
- selects `/Applications/Xcode_16.4.app` explicitly;
- uses its own `.cache/fetchcontent/macos-juce-app` cache directory;
- runs configure, build, and `ctest --preset dev-host --output-on-failure`;
- does not upload a macOS artifact.

Defer macOS app artifact upload, installer creation, signing, and notarization
until those package/release policies are documented separately. Do not bundle
third-party plugins, presets, samples, commercial sounds, logos, or proprietary
assets as part of macOS CI.

## Consequences

- The local macOS path remains `cmake --preset dev-host`, `cmake --build
  --preset dev-host`, and `ctest --preset dev-host --output-on-failure`.
- The project avoids a floating `macos-latest` CI dependency while GitHub's
  hosted runner defaults are changing.
- The follow-up macOS CI task remained small and reviewable: build/test only,
  pinned runner, no artifact.
- The project lacked macOS CI coverage until ADR-0105 implemented that
  follow-up task.
- macOS release packaging remains intentionally unclaimed: no signing,
  notarization, installer, bundled-plugin, preset, sample, or proprietary-asset
  promises are made.

## Follow-Ups

- Build/test-only `macOS JUCE App` CI job implemented by ADR-0105.
- Draft macOS artifact/signing/notarization policy before uploading any
  downloadable macOS CI or release package.
