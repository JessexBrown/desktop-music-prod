<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0094: Audio/MIDI Setup Preferences

## Status

Accepted.

## Context

ADR-0090 added the first-run Audio/MIDI setup prompt as session-only UI state.
Users should not need to dismiss the prompt every launch, and the app should
remember the preferred output intent separately from song/project packages.

The preference path must not weaken real-time audio safety: settings reads,
writes, JSON parsing, and JUCE XML parsing are application-thread work, never
audio callback work.

## Decision

Add a versioned `AppSettings` JSON model in the plain C++ core library. The
settings file is named `settings.json` and is stored under JUCE's per-user
application data location in a `Rabbington Studio` folder.

Persist Audio/MIDI setup state in that file:

- first-run setup prompt dismissal;
- last open output-device summary for human readability;
- JUCE `AudioDeviceManager` restore XML when JUCE has a chosen device state.

On startup, load the settings file before audio device initialization and pass
the saved JUCE device state to `AudioDeviceManager::initialise`. If the settings
file is missing or invalid, use defaults and continue with normal device
initialization.

Only update preferences from UI/application-side control flow: explicit setup
prompt dismissal, setup dialog opening, and Device Panel refreshes that observe
an open output device. Do not write settings when no output device is open, so a
temporary missing device does not erase the last known output intent.

## Consequences

- Audio/MIDI preferences are app state, not project state, and are not copied by
  Save As package operations.
- The settings JSON is testable without JUCE.
- JUCE-specific restore XML remains contained at the app/audio-device-service
  boundary, while the core model also stores readable device metadata.
- Device setup persistence adds no new third-party dependency.

## Follow-Ups

- Add an explicit Reset Audio/MIDI Preferences action after a broader
  application settings surface exists.
- Add a deterministic app smoke hook for simulated saved device state if device
  setup automation becomes necessary.
