<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0030: Imported Media Cache Invalidation

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0028 added a prepared-buffer cache for imported timeline playback,
ADR-0029 added background preparation for cache misses, and ADR-0031 expanded
the cache to multiple bounded entries. Cached media is only valid while the
project clip still points at the same media. Future editing commands will need
to replace or destructively edit imported media, so the domain needs an explicit
invalidation boundary before UI features start relying on cached buffers.

## Decision

Add `ProjectModel::replaceImportedAudioClipMedia` and the matching
`AppSession::replaceImportedAudioClipMedia`.

The model command updates an existing imported `audio-file` clip's package
audio path, analysis path, and length while preserving its timeline start. It
rejects missing clip ids, generated clips, empty media paths, and invalid clip
lengths.

The session command delegates to the model and clears every prepared playback
cache entry whose clip id matches the replaced clip. Project load and project
replacement already clear the cache wholesale.

## Consequences

- Future media-replacement UI and destructive edit commands have a single
  session-domain cache invalidation hook.
- A cached prepared buffer cannot be reused after its clip points at different
  media.
- Timeline placement changes do not clear the cache because the prepared audio
  content is unchanged.
- The command does not copy or decode media; package mutation remains owned by
  import/background jobs.
- No new dependency is introduced.

## Follow-Ups

- Use this command from future clip media replacement UI.
- Add cache invalidation for destructive waveform edits once they exist.
