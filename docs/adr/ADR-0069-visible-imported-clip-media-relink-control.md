<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0069: Visible Imported Clip Media Relink Control

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0067 defined the selected imported-clip media relink chooser flow, and
ADR-0068 added the plain C++ preparation/commit boundary. The next product step
is to expose the behavior in the JUCE right inspector without adding global
shortcuts, command-palette entries, drag/drop relinking, or package asset
cleanup/restoration.

The first visible control must remain selected-clip-only. The inspector can show
a fallback first imported clip when no selected clip is available, but relinking
that fallback clip would be surprising and could mutate the wrong timeline item.

## Decision

Add an inspector-local `Relink` button beside the selected clip start-beat
field. The button is visible only when `ImportedClipInspectorEditDraft` reports
an editable selected imported clip. It is disabled while another audio import
chooser/job, media relink chooser, or timeline playback preparation job is
active.

The button opens a native JUCE `.wav` chooser. When the user picks a file, the
app:

- revalidates that the same selected imported clip is still editable;
- prepares the replacement through `prepareImportedClipMediaRelink`;
- commits through `commitPreparedImportedClipMediaRelink`;
- refreshes the timeline lane and right inspector;
- refreshes app command enablement so undo/redo state updates;
- caches the decoded replacement clip through `AppSession`;
- previews the prepared clip through `AudioDeviceService`.

This slice runs relink preparation synchronously from the chooser completion
callback. That keeps the visible flow small and reuses the tested staging
boundary, but it can block the UI for large WAV files. A background relink job is
added in ADR-0070.

The app command registry is not expanded in this slice. Relink remains an
inspector-local control until the project designs broader command palette,
shortcut, and menu behavior.

## Consequences

- Users can replace the media for the selected imported audio clip from the
  native desktop UI.
- Fallback inspector metadata remains read-only.
- App-session media replacement remains the single place that records undo
  history and invalidates stale prepared playback cache entries.
- Relink preparation and package writes still happen away from the real-time
  audio callback.
- ADR-0070 moves relink preparation to a background job while keeping this
  visible selected-clip control.
- No dependency is added.

## Follow-Ups

- Design package-local media quarantine and restore commands.
- Add command-palette and shortcut entries only after command UX is designed.
