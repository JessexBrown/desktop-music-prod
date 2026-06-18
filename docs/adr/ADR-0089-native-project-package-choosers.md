<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0089: Native Project Package Choosers

## Status

Accepted.

## Context

The first app shell saved and opened a deterministic package named
`Rabbington Studio Demo.project`. That path is useful for smoke tests, but the
DAW needs visible project workflow affordances before users can create and move
between real songs.

Project loading must be recoverable: a missing, malformed, or unsupported
manifest should not replace the current in-memory project.

## Decision

Replace the separate top-bar Save/Open buttons with a compact Project menu.
The menu exposes:

- `New...`: native save-style chooser, creates a new `.project` package only
  when the selected package does not already contain `manifest.json`;
- `Save`: saves the current project to the active package path;
- `Save As...`: native save-style chooser, saves the current project manifest
  and package folder structure to the chosen `.project` path;
- `Open...`: native open chooser, loads a selected `.project` package and only
  swaps the active session after the manifest loads successfully.

The initial active package path remains the deterministic
`Rabbington Studio Demo.project` location in the user's documents folder, so
app smoke tests and deterministic local workflows stay available.

## Consequences

- New/Open/Save As use native desktop choosers instead of hidden deterministic
  paths.
- Invalid package opens surface an error in the status bar and preserve the
  current project/session.
- Import, relink, timeline preparation, and package media maintenance now use
  the active project package path instead of always using the demo package.
- Save As does not yet clone imported media assets from the previous package
  into a new package. That remains a separate package-copy task before Save As
  is considered a full project duplication feature.

## Follow-Ups

- Add package asset cloning or relinking policy for Save As to a different
  directory.
- Add automated UI-level smoke coverage for project New/Open/Save As once a
  deterministic chooser test hook exists.
