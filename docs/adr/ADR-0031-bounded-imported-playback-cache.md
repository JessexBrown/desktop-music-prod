<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0031: Bounded Imported Playback Cache

## Status

Accepted for the v0.1 prototype. Amended by ADR-0032, which adds an explicit
prepared-sample byte budget.

## Context

ADR-0028 added the first prepared-buffer cache for imported timeline playback.
That cache proved the realtime-safe handoff shape, but it only retained one
imported clip. As soon as a project has multiple imported clips, replacing one
entry on every import or preparation forces avoidable background preparation and
makes reopened or edited projects feel less responsive.

The prototype still does not mix overlapping imported clips or stream large
files. The next useful step is a small bounded cache that improves clip
selection and preparation behavior without widening the audio engine.

## Decision

Store a bounded collection of imported timeline prepared-buffer entries in
`AppSession`.

Each entry is keyed by:
- imported clip id;
- package-relative media path.

The cache:
- stores immutable prepared sample buffers behind
  `shared_ptr<const std::vector<float>>`;
- accepts only clips that still exist in the current project and still point to
  the same media path;
- is capped by the prepared-sample byte budget recorded in ADR-0032;
- keeps entries in insertion/update order;
- replaces an existing entry when the same clip id and media path are cached
  again;
- evicts the oldest entry when more than four entries are stored;
- clears wholesale on project replacement and project load;
- clears every entry for a clip id when media replacement succeeds.

Timeline playback lookup still asks for the first relevant imported clip from
the current transport position. If the matching prepared buffer is cached, the
session schedules it without decoding. If it is not cached, the session returns
the existing background-preparation-required status.

## Consequences

- Recently imported or prepared multi-clip arrangements can reuse more than one
  prepared buffer.
- Cache eviction is deterministic and bounded.
- Stale entries are rejected even if a caller mutates a clip's media path
  without going through the session invalidation command.
- The cache remains a session-domain optimization, not a project-file feature.
- The audio callback still receives only an already-prepared immutable sample
  buffer.
- No new dependency is introduced.

## Follow-Ups

- Add user-facing or project-level cache preferences only after profiling real
  imported audio sessions.
- Add destructive waveform-edit invalidation when those commands exist.
- Extend timeline playback beyond one selected imported clip when arrangement
  mixing is introduced.
