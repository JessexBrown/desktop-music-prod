<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0064: Imported Clip Inspector Editing Flow

## Status

Accepted for the v0.1 prototype.

## Context

The right inspector can show selected imported clip metadata, and imported clip
placement plus media replacement now have plain C++ undo/redo history and app
command routing. The next UI step is to make inspector fields editable without
stealing keyboard focus from timeline navigation or creating one-way mutations.

The current inspector may fall back to the first imported clip when no imported
clip is selected. That fallback is useful for empty-state discovery, but it
would be risky for editing because a user could modify a clip they did not
explicitly select.

## Decision

The first editable imported clip inspector will only allow edits when
`ImportedClipInspectorState::usingSelectedClip` is true. Fallback inspector
content remains read-only and should prompt the user to select a clip before
editing.

Editable fields for the first implementation:

- placement start beat;
- media path display plus relink action;
- analysis path display as read-only derived metadata;
- clip length in beats after relink or explicit length correction.

Text-entry focus belongs to the focused field while editing. While a text field
has focus:

- arrow keys move the caret or selection inside the field;
- Return commits the field;
- Escape cancels and restores the last committed value;
- Tab commits the field and moves to the next inspector field;
- global shortcuts and focused timeline shortcuts do not run unless the text
  field declines the key event.

Placement start-beat commits call
`AppSession::setImportedAudioClipStartBeats`. Each accepted committed value
records at most one placement undo entry. Typing intermediate characters,
invalid values, and cancelled edits do not record undo entries.

Media relink commits call `AppSession::replaceImportedAudioClipMedia` after a
future file chooser or relink resolver has produced package-relative
`relativePath`, `analysisPath`, and `lengthBeats` values. A successful relink
records one media replacement undo entry and invalidates prepared playback cache
through the existing session boundary.

Validation rules:

- start beat must be finite and non-negative;
- length beats must be finite and positive;
- media path must be present and package-relative;
- analysis path may be empty only when analysis has not been generated yet;
- validation errors keep focus on the field and show a recoverable status
  message without changing project state.

Undo grouping is commit-based:

- one committed placement field edit equals one undo entry;
- one committed relink equals one undo entry;
- repeated commits are separate entries unless a later explicit coalescing
  policy is designed;
- undo/redo uses the existing `edit.undo` and `edit.redo` command ids.

This slice does not add visible editable controls, shortcuts, command-palette
bindings, drag/drop placement, package file copying/deletion/restoration, or a
file chooser for relink.

## Consequences

- Inspector editing has a focus policy before controls are introduced.
- Users cannot accidentally edit the first imported clip fallback.
- The design preserves timeline keyboard navigation while text fields are
  active.
- Existing app-session undo/redo and cache invalidation remain the edit commit
  boundary.
- The audio callback remains untouched.
- No dependency is added.

## Follow-Ups

- Add visible native relink chooser wiring.
