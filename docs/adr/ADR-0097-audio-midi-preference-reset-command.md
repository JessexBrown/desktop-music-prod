<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0097: Audio/MIDI Preference Reset Command

## Status

Accepted.

## Context

Rabbington Studio stores Audio/MIDI setup reminder dismissal and preferred output
intent in app-level JSON outside `.project` packages. Users need a visible way
to return to first-run setup behavior without deleting projects or manually
finding the settings file.

## Decision

Add an `audio.settings.reset` app command and expose it as the Device Panel
`Reset Prefs` action when the first-run `Dismiss` action is not using that
secondary action slot.

The reset command clears `AppSettings::audioSetup`, persists the settings file,
and leaves the currently open audio device and project package untouched. Output
preference auto-persistence now waits until the setup prompt has been dismissed
or the Audio/MIDI setup dialog has been opened, so a reset is not immediately
repopulated by the current device summary.

Register a hidden JUCE app smoke mode, `--smoke-audio-midi-reset`, with CTest.
The smoke test seeds an isolated temporary app settings file, dispatches the
same reset command used by the Device Panel action, reloads the settings file,
and verifies first-run dismissal and preferred output intent are cleared while
the active project package path remains unchanged. The smoke hook is not exposed
through normal UI.

## Consequences

- Users can recover from stale Audio/MIDI restore preferences without removing
  project packages.
- Settings file reads/writes stay on the UI/settings path, not the audio
  callback thread.
- Current audio playback can continue using the already-open device until the
  user changes setup or restarts.
- CI can catch regressions where reset accidentally repopulates settings,
  mutates project package state, or writes to the user's real settings path.
