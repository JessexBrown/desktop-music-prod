<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0028: Imported Playback Prepared Buffer Cache

## Status

Accepted for the v0.1 prototype. Superseded in part by ADR-0031, which replaces
the one-entry cache with a bounded multi-entry cache.

## Context

ADR-0027 wired imported timeline playback into the app/session Play command.
That first path was realtime safe because it decoded project audio before
scheduling the audio engine, but a cache miss could still decode a WAV file on
the UI thread when Play was pressed.

The app already receives a decoded prepared mono buffer after background audio
import. Reusing that prepared buffer is the smallest step toward responsive
timeline playback without introducing a broader streaming, resampling, or
multi-track engine.

## Decision

Store one imported timeline clip's prepared mono buffer in `AppSession`.

ADR-0031 later expands this to a bounded multi-entry cache while preserving the
same realtime-safe prepared-buffer handoff.

The cache:

- only accepts clips that still exist in the current project and still point to
  the same package-relative audio path;
- is invalidated by the session media-replacement command recorded in ADR-0030;
- stores prepared samples behind `shared_ptr<const std::vector<float>>`;
- clears when a project is replaced or loaded;
- is populated by direct session import and by the JUCE import result handoff;
- is preferred by `playFromTimeline` before trying to decode package audio.

`AudioEngineStub` and the JUCE audio service now accept shared prepared-sample
buffers. The buffer pointer is installed outside the audio callback; the callback
only indexes already-prepared samples and advances counters.

If no cache entry is available, `playFromTimeline` can still decode the package
PCM16 WAV as a compatibility fallback for core callers. ADR-0029 moves the JUCE
Play button's cache-miss path onto a background preparation job.

## Consequences

- Pressing Play after importing a clip reuses the import-prepared buffer instead
  of decoding the copied WAV again.
- Missing package audio can still play when the imported clip is cached.
- Clearing the cache or reopening the project can use the background preparation
  job recorded in ADR-0029.
- Replacing imported clip media clears the matching cache entry before the new
  media can be scheduled.
- ADR-0031 expands the prototype to cache a bounded set of imported clips, but
  the system still does not mix overlapping clips or stream large files.
- No new dependency is introduced.

## Follow-Ups

- Revisit prepared-cache policy when streaming and multi-clip mixing land.
- Add destructive waveform-edit invalidation when those commands exist.
