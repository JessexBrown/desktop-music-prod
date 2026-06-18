<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0058: Selected Clip Timeline Centering Helper

## Status

Accepted for the v0.1 prototype.

## Context

Imported audio clips can now be selected by mouse or keyboard, and the
workspace has compact pan, reset, fit, and zoom controls. A future
center-selected control should not duplicate timeline geometry math in the JUCE
component, especially because loaded projects may preserve stale selected clip
ids for recovery.

The behavior should be proven in plain C++ before adding another visible
control.

## Decision

Add `centerTimelineViewportOnSelectedImportedAudioClip` to the app-session core
model. The helper:

- returns no value when there is no selected clip id;
- returns no value when the selected id is stale, non-audio, non-finite, or has
  a non-positive length;
- returns no value for invalid viewport widths;
- preserves the current normalized beats-per-pixel zoom scale;
- computes a clamped viewport start that centers the selected imported clip in
  the current lane width.

The tests cover no selection, stale selected id loaded from a project manifest,
invalid viewport width, and an offscreen selected imported clip whose lane
rectangle is centered after applying the helper result.

This ADR does not add a visible center button, clip editing, drag/drop behavior,
or global shortcuts.

## Consequences

- Selected-clip centering behavior is testable without JUCE.
- Stale selected ids remain recoverable and do not silently fall back to another
  clip for viewport navigation.
- Future UI can call a small model helper and keep viewport math centralized.
- No audio callback behavior changes.
- No dependency is added.

## Follow-Ups

- Add package media quarantine preflight plan model.
- Keep global shortcuts and command palette work behind the command registry
  focus policy.
