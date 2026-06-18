<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0072: Imported Media Package Inventory Model

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0071 defines imported media cleanup as an inventory-first workflow. Before
the project can add any package cleanup or restoration command, it needs a
plain C++ dry-run model that can describe package-owned media without deleting
files or coordinating with UI controls.

The package currently stores imported audio under `audio/`, waveform summaries
under `analysis/`, previous manifest backup state under
`backups/manifest.previous.json`, and active import/relink staging under
`.projectname-staging/`.

## Decision

Add `ImportedMediaPackageInventory` to `projectname_core`.

The inventory builder:

- parses the current `manifest.json` for imported `audio-file` clip media and
  analysis references;
- parses `backups/manifest.previous.json` when it exists;
- accepts optional caller-provided session-protected references for future
  undo/redo integration;
- scans existing regular files under package `audio/` and `analysis/`;
- reports valid references whose package files are missing;
- reports unsafe references that are absolute, escaping, non-normalized, or in
  the wrong package folder;
- reports direct directories under `.projectname-staging/` and marks them stale
  only when no package work is active.

The model returns structured data only. It does not delete, move, quarantine,
restore, save, open, log from the audio callback, scan plugins, or touch UI
state.

## Consequences

- Future cleanup UI can start from a deterministic dry-run result instead of
  reimplementing package scans.
- Current manifest, previous-manifest backup, and session-protected references
  can all protect assets from cleanup candidates.
- Unsafe path handling is explicit before any destructive package maintenance
  exists.
- Background import/relink staging is visible to tests without being removed.
- No dependency is added.

## Follow-Ups

- Add package media quarantine restore-manifest model tests.
- Define backup-manifest retention before any permanent media deletion task.
- Expose inventory results in a future package maintenance UI after quarantine
  behavior is designed.
