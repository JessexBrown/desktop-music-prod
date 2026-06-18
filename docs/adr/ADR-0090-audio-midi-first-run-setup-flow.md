<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0090: Audio/MIDI First-Run Setup Flow

## Status

Accepted.

## Context

Rabbington Studio already initializes a default JUCE audio device and exposes an
Audio/MIDI setup button. A new user still needs clearer first-run feedback when
the default device works, and clearer recovery when no output device is
available.

This milestone must stay separate from plugin scanning. Audio setup is a
device-selection concern, while plugin scanning is unsafe work that will need
background execution, timeouts, quarantine, and policy updates.

## Decision

Add a small plain C++ Audio/MIDI setup status model that converts initialization
state, current output-device state, and a per-session dismissal flag into a
Device Panel view model.

Bind that model into the JUCE app by:

- showing a Device Panel setup action that opens the existing Audio/MIDI
  selector;
- showing a first-run dismiss action only for the current app session;
- surfacing failed initialization and missing-output states without blocking
  project work;
- keeping the top-bar Audio/MIDI command as a persistent non-modal path back to
  setup.

## Consequences

- The first-run device setup prompt is testable without creating a desktop
  window or audio device.
- Users can recover from unavailable audio output through the Device Panel or
  top bar.
- The flow remains intentionally separate from plugin scanning and does not add
  any new dependency.
- Dismissal is per app session only; persisted onboarding preferences remain a
  later task.

## Follow-Ups

- Persist device setup dismissal and preferred device intent after the project
  has an application settings file.
- Add deterministic UI smoke coverage for unavailable-device presentation if
  the app gains a test hook for simulated audio-device state.
