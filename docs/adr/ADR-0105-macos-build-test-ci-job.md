<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0105: macOS Build-Test CI Job

## Status

Accepted.

## Context

Rabbington Studio needs macOS coverage for the cross-platform native desktop
DAW goal. ADR-0103 documented the macOS runner and Xcode assumptions before
enabling a hosted CI job. The existing CI matrix already builds and tests the
JUCE app on Windows and Linux, while the `dev-host` preset is the documented
local build path for macOS and Linux host generators.

The macOS CI job should prove that the current native JUCE app configures,
builds, and runs the automated smoke/unit test suite on a hosted macOS runner.
It should not create release packaging policy by accident.

## Decision

Add a build/test-only GitHub Actions job named `macOS JUCE App`.

The job:

- runs on `macos-15`, not `macos-latest`;
- selects Xcode 16.4 explicitly with
  `/Applications/Xcode_16.4.app/Contents/Developer`;
- uses the existing `dev-host` CMake preset;
- sets `FETCHCONTENT_BASE_DIR` to
  `.cache/fetchcontent/macos-juce-app`;
- uses a dedicated `macos-juce-app-fetchcontent-` cache key;
- runs configure, build, and `ctest --preset dev-host --output-on-failure`;
- does not upload a macOS app artifact.

Do not add macOS signing, notarization, installer creation, appcast creation,
release packaging, plugin scanning, or bundled plugins, presets, samples,
commercial sounds, logos, or proprietary assets in this CI job.

## Consequences

- Pushes and pull requests now verify the JUCE desktop app on macOS in addition
  to Windows and Linux.
- The macOS job increases CI time but keeps the first macOS coverage path small
  and reviewable.
- macOS release/distribution remains intentionally unclaimed. There is no
  downloadable macOS CI artifact, no signed `.app`, no notarized package, and no
  installer.
- Runner image content can still change over time, so the job pins the runner
  label and Xcode path but should be reviewed when GitHub's macOS hosted-runner
  images change.

## Follow-Ups

- Draft macOS artifact, signing, notarization, and installer policy before
  uploading any macOS app package from CI.
- Investigate a macOS-specific fallback only if the hosted smoke tests expose a
  runner GUI/audio limitation.
