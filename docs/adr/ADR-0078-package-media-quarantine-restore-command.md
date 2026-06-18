<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0078: Package Media Quarantine Restore Command

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0073 defined package-local quarantine and restore behavior. ADR-0074 added
the restore-manifest model, ADR-0075 added preflight planning, and ADR-0077
added the command that moves package media into quarantine.

The restore half now needs a plain C++ command that can safely move quarantined
items back without overwriting active project media.

## Decision

Extend `PackageMediaQuarantineCommand` with `restorePackageMediaFromQuarantine`.

The restore command accepts:
- a package directory;
- one package-local `restore-manifest.json`;
- an optional list of selected original package-relative paths.

The command loads and validates the manifest, restores all selected unrestored
entries when their original package-relative paths are empty, marks occupied
original paths as restore conflicts, records missing quarantine paths as
partial-failure entry errors, and persists the updated manifest through
`restore-manifest.json.tmp`.

If every moved entry is restored, the manifest state becomes `restored`. If a
selected subset restores while other entries remain quarantined, the manifest
stays `completed` with per-entry `restored` flags. If occupied originals are
encountered, the manifest state becomes `restore-conflict` and no active media
is overwritten. If quarantine files or directories are missing, the manifest
state becomes `partial-failure`.

The command does not permanently delete quarantine contents, define retention
policy, create UI, route command-palette actions, scan plugins, mutate
`manifest.json`, or run on the real-time audio callback.

## Consequences

- Package-local cleanup is now reversible through tested plain C++ primitives.
- Future UI can expose cleanup and restore flows without duplicating package
  path safety rules.
- Missing-media recovery can later use restored package-relative files without
  requiring project manifest rewrites.
- No dependency is added.

## Follow-Ups

- Add a background-job wrapper for quarantine and restore operations.
- Design visible package maintenance UI after background execution is in place.
- Define retention rules before any permanent deletion command.
