<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0032: Prepared Playback Cache Memory Budget

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0031 expanded imported timeline playback from a one-entry prepared-buffer
cache to a small bounded multi-entry cache. Entry count alone is not enough for
audio files: one long imported clip can consume far more memory than several
short clips. The prototype needs an explicit memory policy before larger audio
file support, resampling, or timeline mixing increases cache pressure.

## Decision

Add `ImportedTimelineClipCacheLimits` to `AppSession`.

The default limits are:
- at most four prepared imported timeline clip entries;
- at most 32 MiB of cached prepared sample data.

The session cache:
- estimates sample memory as `sampleCount * sizeof(float)`;
- rejects a single prepared buffer that is larger than the byte budget;
- leaves transport state unchanged when an oversized buffer is rejected;
- evicts oldest entries after insert until both entry count and sample-byte
  limits are satisfied;
- keeps media-replacement invalidation scoped to matching clip ids.

The policy is enforced outside the audio callback while prepared buffers are
handed to the audio engine as immutable shared sample vectors.

## Consequences

- Cache memory behavior is deterministic and testable.
- Small projects can retain several recently prepared imported clips.
- Oversized clips fall back to the existing background preparation path instead
  of staying resident in the session cache.
- The budget counts prepared sample bytes only; metadata, vector capacity, and
  allocator overhead are intentionally excluded for the prototype.
- No new dependency is introduced.

## Follow-Ups

- Add user-facing or project-level cache preferences only after profiling real
  imported audio sessions.
- Track actual allocation pressure if the simple sample-byte estimate proves too
  weak.
- Revisit cache policy when streaming, resampling, and multi-clip mixing land.
