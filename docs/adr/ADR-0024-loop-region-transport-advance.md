<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0024: Loop Region Transport Advance

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0023 added validated, persisted loop-region state. The next Milestone 3
step is to make the domain transport position honor that state during ordinary
session advancement while keeping the realtime audio callback free of project
manifest access, JSON parsing, UI calls, and other non-realtime work.

The current UI timer already advances the project-backed transport through
`AppSession::advanceSeconds`. That makes the session layer the smallest safe
place to apply loop wrapping for the prototype.

## Decision

Apply enabled loop regions in `AppSession::advanceSeconds` after the underlying
`TransportState` advances.

The wrapping rule:

- disabled loop regions leave transport advancement unchanged;
- stopped transport remains unchanged;
- crossing or landing exactly on loop end wraps to loop start;
- overshoot is preserved with modulo arithmetic against the loop length;
- large overshoots across multiple loop lengths resolve deterministically.

`TransportState` remains a generic play/stop/tempo/position container and does
not learn about project loop metadata. The audio engine and audio callback are
not changed by this slice.

## Consequences

- The app-domain transport position now behaves like a loop-aware arrangement
  cursor for UI/model advancement.
- Loop behavior is covered by plain C++ tests for disabled loops, enabled loops,
  exact loop end, stopped transport, and overshoot.
- Audio rendering is still a later slice; this does not yet schedule imported
  clips through the audio callback according to the loop region.
- No new dependency is introduced.

## Follow-Ups

- Loop range layout and native rendering are recorded in ADR-0025.
- Connect loop-aware session position to timeline audio scheduling once the
  imported clip scheduling model expands beyond the prepared-buffer proof.
- Add user-facing loop controls after project chooser work reduces the
  deterministic demo-project coupling.
