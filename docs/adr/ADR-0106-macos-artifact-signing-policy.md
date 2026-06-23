<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0106: macOS Artifact Signing Policy

## Status

Accepted.

## Context

Rabbington Studio now has a `macOS JUCE App` CI job that configures, builds, and
tests the native desktop app, but intentionally uploads no macOS app artifact.
The project needs policy before enabling any downloadable macOS package so CI
does not accidentally imply release readiness.

Apple's current Developer ID path for apps distributed outside the Mac App Store
expects Developer ID signing, notarization by Apple, and Gatekeeper-compatible
testing. Apple also documents Hardened Runtime as required for notarization
uploads. The project is free/open-source and must keep release packaging legally
clean: no bundled proprietary plugins, presets, samples, commercial sounds,
logos, or protected assets.

## Decision

Keep the current macOS CI job build/test-only until a separate implementation
adds artifact staging, exact allowlist verification, checksum generation, and
clear user messaging.

Unsigned macOS `.app` bundles are allowed only as transient CI build outputs and
local developer outputs. A future short-retention macOS CI artifact may upload an
unsigned debug/smoke `.app` only if the artifact is explicitly labeled as:

- an unsigned CI debug build;
- not a release installer or production distribution;
- likely to trigger macOS Gatekeeper warnings or launch friction;
- provided for contributor inspection after tests pass;
- containing only first-party app files, `LICENSE`, `README.md`,
  `docs/DEPENDENCIES.md`, an artifact note, and checksums;
- containing no bundled plugins, presets, samples, commercial sounds, logos,
  proprietary assets, dependency checkouts, CMake build trees, or caches.

Public macOS release packages distributed outside the Mac App Store must wait
for a separate release-packaging change that:

- uses an Apple Developer Program team and Developer ID signing identity;
- signs the app bundle and every nested executable, helper, framework, and
  plugin-like first-party binary from the inside out;
- enables Hardened Runtime and keeps entitlements minimal;
- uses `xcrun notarytool`, not legacy notarization upload paths;
- staples and validates the notarization ticket where the chosen package format
  supports it;
- verifies signing and Gatekeeper assessment before publishing;
- signs installer packages with Developer ID Installer when a `.pkg` installer
  is introduced;
- documents the exact package format, user-facing install path, and update
  strategy before publishing.

The project will not add Apple signing certificates, private keys, provisioning
profiles, App Store Connect credentials, or notarization credentials to source
control. Any future CI signing credentials must live in protected repository or
environment secrets, must not run for untrusted fork pull requests, and must not
be written to artifacts, caches, or logs.

The Mac App Store is not a target for this policy. If the project later chooses a
Mac App Store path, it needs a separate ADR because sandboxing, review, purchase,
and plugin-hosting implications are materially different.

Plugin-hosting entitlements are also deferred. In particular, any future use of
library-validation exceptions or other Hardened Runtime relaxations for loading
user-installed Audio Unit, VST3, CLAP, or LV2 plugins must be justified in the
plugin-hosting architecture work and reflected in `docs/PLUGIN_POLICY.md`.

## Consequences

- macOS build/test coverage remains active without creating a misleading
  downloadable package.
- Future unsigned macOS CI artifacts are permitted only for short-lived smoke
  inspection and only with explicit warnings.
- Public macOS releases require Developer ID signing, notarization, hardened
  runtime review, stapling/validation where applicable, and Gatekeeper testing.
- Signing secrets and notarization credentials stay out of the repository and
  out of routine pull-request CI.
- The policy preserves the project's legal cleanliness by forbidding bundled
  proprietary plugins, presets, samples, commercial sounds, logos, and protected
  assets in macOS artifacts.

## Follow-Ups

- If a macOS CI artifact is added, mirror the Windows/Linux artifact allowlist
  and checksum gate before upload.
- Update or retire `tools/check_ci_macos_artifact_upload_guard.cmake` and its
  fixture test in the same reviewed change that intentionally adds macOS
  artifact staging, signing, notarization, or installer packaging.
- Add release packaging scripts only after the exact macOS package format is
  selected and documented.
- Revisit entitlements during the plugin-host prototype, especially for
  user-installed plugin loading under Hardened Runtime.

## Source Review

Last verified: 2026-06-23.

- Apple Developer, "Signing Mac Software with Developer ID":
  `https://developer.apple.com/developer-id/`
- Apple Developer Support, "Developer ID":
  `https://developer.apple.com/support/developer-id/`
- Apple Developer, "Distributing software on macOS":
  `https://developer.apple.com/macos/distribution/`
- Apple Developer Documentation, "Notarizing macOS software before distribution":
  `https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution`
- Apple Developer Documentation, "Hardened Runtime":
  `https://developer.apple.com/documentation/security/hardened-runtime`
