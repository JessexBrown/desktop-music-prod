# ADR-0006: Project Device Chain Placeholders

## Status

Accepted for the v0.1 prototype.

## Context

Milestone 2 calls for project persistence that includes tracks, clips, tempo,
time signature, and device-chain placeholders. The app UI already has a device
panel placeholder, but the project manifest only persisted tracks and clips.

The project must stay legally clean. Adding project-level device placeholders
must not imply that third-party plugins, proprietary presets, or commercial
sounds are bundled.

## Decision

Add a small `ProjectDevice` model to `projectname_core` and store a `devices`
array on each project track in `manifest.json`.

The first fields are:
- `id`
- `name`
- `type`
- `bypassed`

The default project includes one original built-in placeholder:
`builtin/generated-tone-source`.

Keep `devices` optional when loading manifest version 1 so older first-slice
project packages without device chains still load. Reject malformed device-chain
data, such as a non-array `devices` value or a device without an `id`.

Do not add plugin state, plugin scan paths, plugin binary references, presets, or
parameter automation in this step.

## Consequences

- The project package now persists the first device-chain placeholder needed by
  the UI skeleton and future built-in devices.
- Existing manifest version 1 packages remain loadable because the field is
  additive and optional on read.
- Future plugin work still needs a separate policy/architecture decision and
  must follow `docs/PLUGIN_POLICY.md`.

## Follow-Ups

- Add typed built-in device definitions before implementing real effects.
- Add missing-plugin placeholders when plugin hosting begins.
- Add parameter state and automation only after the core device model is stable.
