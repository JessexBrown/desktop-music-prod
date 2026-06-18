<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0066: Visible Imported Clip Start Beat Control

## Status

Accepted for the v0.1 prototype.

## Context

Imported clip placement, media replacement, undo/redo, command routing, and the
plain C++ inspector edit draft model are already in place. The right inspector
still exposes imported clip metadata mostly as read-only lines, so users cannot
yet correct a selected clip's timeline position from the inspector.

ADR-0064 requires that editable inspector fields only mutate explicitly selected
clips, not the first imported clip fallback. ADR-0065 requires visible controls
to validate through `ImportedClipInspectorEditDraft` before calling session
commit commands.

## Decision

Add a native JUCE `TextEditor` row to the right inspector for the selected
imported clip start beat.

The control is visible only when `ImportedClipInspectorState::usingSelectedClip`
is true. When the inspector is showing the first imported clip fallback, the
same row remains read-only text in the painted inspector lines.

Return commits the current text through
`ImportedClipInspectorEditDraft::makeStartBeatCommit`, then applies the accepted
value with `AppSession::setImportedAudioClipStartBeats`. Successful commits
refresh the timeline lane, inspector, and app command enablement so the updated
clip position and undo availability are visible immediately.

Escape calls `ImportedClipInspectorEditDraft::cancelStartBeatEdit` and restores
the last committed value in the text field without touching the project. Invalid
text keeps focus in the field, selects the text, and reports a recoverable
status message.

This slice does not add media relink UI, native file chooser behavior, package
file restoration, global shortcuts, command-palette entries, drag/drop clip
operations, or automated GUI interaction tests.

## Consequences

- Selected imported clips can now be repositioned from the inspector without
  drag/drop.
- First-clip fallback inspector content remains read-only.
- Inspector start-beat edits share the existing app-session undo/redo boundary.
- Text editing focus stays inside the JUCE text field instead of the workspace
  shortcut router.
- The audio callback remains untouched.
- No dependency is added.

## Follow-Ups

- Design imported clip media relink chooser flow.
- Add automated GUI interaction coverage when a stable JUCE UI test approach is
  available.
