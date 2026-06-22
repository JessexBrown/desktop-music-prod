<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0099: CMake Presets SPDX Metadata

## Status

Accepted.

## Context

`CMakePresets.json` is a first-party build configuration file, but JSON does not
support comments. The repository SPDX checker previously kept it in
`docs/SPDX_EXCEPTIONS.txt` because there was no top-of-file comment syntax for a
traditional license header.

CMake presets support a top-level `vendor` object for vendor-specific metadata.
That field is valid CMake presets JSON and is ignored by CMake when it does not
understand the vendor namespace.

## Decision

Store the repository SPDX identifier for `CMakePresets.json` in the top-level
`vendor` object under `rabbington.studio/license`.

Remove `CMakePresets.json` from `docs/SPDX_EXCEPTIONS.txt` and keep the SPDX
check pointed at the first few lines of first-party files so the metadata remains
machine-checkable.

## Consequences

- `CMakePresets.json` remains valid CMake presets JSON.
- The first-party SPDX exception baseline is empty.
- Future JSON configuration files should use schema-supported metadata when
  available instead of adding avoidable exceptions.
