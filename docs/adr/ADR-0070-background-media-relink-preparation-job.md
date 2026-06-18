<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0070: Background Media Relink Preparation Job

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0069 added a visible selected-clip media relink control in the JUCE
inspector, but it prepared the chosen WAV synchronously from the native chooser
completion callback. That kept the first visible flow small, but large files
could block the UI while the app decoded, copied, and analysed the replacement.

The relink commit must still remain guarded by the current `AppSession` state:
background work can finish after the selected clip changes, and stale completion
must not mutate the wrong clip.

## Decision

Add `BackgroundMediaRelinkPreparationJob` to `projectname_core`.

The job owns a project snapshot, package directory, source PCM16 WAV path, and
selected imported clip id. It starts an async worker that calls
`prepareImportedClipMediaRelink` with cancellation and progress callbacks. The
job reports:

- decode frame progress;
- staged-copy byte progress;
- decoding, copying, analysing, completed, failed, and cancelled phases;
- a prepared relink payload for later UI-thread commit.

The worker only prepares and stages files. It does not mutate the live
`AppSession`, update JUCE components, touch audio services, or perform final
project commit.

The JUCE inspector now starts this background job after the native WAV chooser
returns. While the job is running, the selected-clip `Relink` button remains
visible but disabled, the start-beat field is hidden to avoid concurrent
selected-clip edits, and a local `Cancel` button requests cancellation. The app
polls the job from the UI timer and commits only completed current-selection
results through `commitPreparedImportedClipMediaRelink`.

## Consequences

- Relink decode/copy/analysis work no longer blocks the UI after file
  selection.
- Cancellation is available before final project mutation.
- Stale selection is still rejected by the existing commit helper.
- App-session media replacement remains the only place that records undo
  history and clears stale prepared playback cache entries.
- Relink package cleanup/restoration remains a separate design task.
- No dependency is added.

## Follow-Ups

- Design imported media package cleanup and restoration.
- Add command-palette and shortcut entries only after command UX is designed.
