<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0041: Imported Clip Inspector

## Status

Accepted for the v0.1 prototype. Extended by ADR-0042 with deterministic
imported clip selection state.

## Context

The app shell has had a right-side inspector placeholder since the first UI
slice, but imported audio clip details were only visible indirectly through
timeline labels, status messages, and project files. ADR-0040 added explicit
sample-rate mismatch metadata for prepared playback, and the next small UX step
is to show imported clip metadata before building full selection, relinking, or
editing workflows.

## Decision

Add a read-only imported clip inspector view-model in the core layer. It selects
the first imported audio clip in project track order and loads its waveform
summary metadata from the project package. The view-model reports:
- track and clip identity;
- package-relative audio and analysis paths;
- clip start and length in beats;
- source sample rate;
- source frame count;
- source duration in seconds;
- whether the source sample rate differs from the current output device rate.

The JUCE app refreshes the right-side inspector from this view-model after
initial audio setup, Save, Open, Import, and output sample-rate changes. The UI
renders compact read-only rows and surfaces the ADR-0040 warning when the first
imported clip rate differs from the current audio device rate.

This slice did not add clip selection, file relinking, resampling, waveform
editing, or inspector-editable fields. ADR-0042 and ADR-0043 later added
deterministic imported clip selection and timeline hit-testing.

## Consequences

- Users can inspect real imported audio metadata from the app shell instead of
  reading the manifest or analysis JSON.
- The implementation is deterministic and follows persisted timeline clip
  selection state once ADR-0042/ADR-0043 behavior is available.
- Missing or invalid analysis metadata is shown as a recoverable inspector state.
- No work moves onto the audio callback; the inspector view-model is used from
  UI/session paths only.
- No new dependency is introduced.

## Follow-Ups

- Add a compact selected-clip action surface before editable clip operations.
- Add non-destructive clip placement/edit controls after selection and undo
  boundaries exist.
- Add resampling before treating mismatched imported audio as production ready.
