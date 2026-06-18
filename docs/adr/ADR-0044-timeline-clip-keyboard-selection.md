<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0044: Timeline Clip Keyboard Selection

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0043 made imported audio clip selection possible with mouse hit-testing in
the JUCE timeline lane. GOAL.md also requires major actions to be reachable by
keyboard, and the next small task is to let users move selection between
imported clips without adding movement, resizing, relinking, waveform editing,
or undoable edit operations.

Selection remains app/project state. Keyboard traversal must not touch the audio
callback and should use the same persisted selected clip id as mouse selection
and the inspector.

## Decision

Add `ImportedAudioClipSelectionDirection` and
`selectAdjacentImportedAudioClip` to `ProjectModel`, then expose the same command
through `AppSession`.

Adjacent selection uses timeline order: imported audio clips are sorted by start
beat, then by their stable project source order. `next` and `previous` wrap at
the ends. If the current selection is empty or stale, `next` selects the first
timeline-ordered imported clip and `previous` selects the last.

The JUCE workspace panel accepts keyboard focus when clicked. While focused, the
left arrow selects the previous imported audio clip and the right arrow selects
the next. `MainComponent` refreshes the timeline lane and inspector after the
session command so keyboard and mouse selection share the same visible state.

## Consequences

- Imported clips can now be selected by keyboard in the native workspace.
- Empty or stale selection has deterministic recovery behavior.
- Generated clips are ignored by adjacent imported-audio selection.
- No timeline edit operation, drag/drop, relinking, waveform editing, or plugin
  work is introduced.
- No audio callback behavior changes.
- No new dependency is introduced.

## Follow-Ups

- Add a command routing layer before global shortcuts, command palette, or text
  editing surfaces expand.
- Add undo boundaries before editable inspector or timeline clip operations.
- Add timeline viewport scroll/zoom state before expanding arrangement editing.
