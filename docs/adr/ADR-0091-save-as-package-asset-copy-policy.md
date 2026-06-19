<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0091: Save As Package Asset Copy Policy

## Status

Accepted.

## Context

ADR-0089 added native `Save As...` package selection, but the first
implementation only wrote the project manifest and package folder structure.
That is unsafe once a project contains package-local imported media: a new
package manifest can keep `audio/...` and `analysis/...` references without the
files those references need.

Package copying must stay away from the real-time audio callback. It also needs
tests before it becomes a file-moving UI workflow.

## Decision

Add a plain C++ Save As relocation policy model that can be tested without
native file choosers.

When Save As targets a different package:

- package-local `audio/`, `analysis/`, `samples/`, and `presets/` contents are
  the creative assets that a future copy command must clone;
- `backups/` starts fresh in the target package, because manifest backups and
  media-trash restore history describe the source package's history;
- absolute external media references stay explicit in the manifest and are not
  copied silently;
- unsafe or unsupported relative references are reported as warnings and are not
  copied;
- missing package-local references remain missing and recoverable through the
  normal missing-media surfaces.

Until the copy command exists, the app refuses manifest-only Save As when the
policy detects package-local assets that need cloning. The status bar records a
relocation warning instead of creating a misleading target package.

## Consequences

- Save As no longer silently creates a different package with package-local
  media references but no copied media.
- Users can still Save As projects that have no package-local media to copy, or
  that preserve explicit external references.
- Backups are intentionally not cloned into the new package.
- The next implementation step is a package-writer copy command that runs off
  the audio callback and uses this policy as its preflight.

## Follow-Ups

- Implement the package asset copy command for `audio/`, `analysis/`,
  `samples/`, and `presets/`.
- Wire Save As to run the copy command before saving the manifest and switching
  the active package path.
