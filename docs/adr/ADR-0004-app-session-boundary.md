<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0004: App Session Boundary

## Status

Accepted for the v0.1 prototype.

## Context

The app shell, fallback launcher, project persistence, and tests must agree about
transport state. The first draft had UI-owned transport state next to a separate
project-owned transport state, which could let the transport shown in the UI
drift from the transport saved in a project manifest.

The goal document recommends a thin app-domain interface so future engine choices
do not leak directly into the UI.

## Decision

Introduce `projectname::AppSession` in the plain C++ core.

`AppSession` owns the current `ProjectModel`, exposes the project-backed
`TransportState`, and records whether the generated test tone should be active.
The JUCE app and Win32 fallback both drive playback through this object instead
of keeping separate transport state. Project save/load actions also pass through
this object so UI code does not duplicate persistence behavior.

## Consequences

- Project save/load state, UI transport state, and tests now use the same
  transport object.
- The app-domain boundary remains testable without JUCE.
- Future engine work can attach to the session boundary without requiring the UI
  to own engine internals.
