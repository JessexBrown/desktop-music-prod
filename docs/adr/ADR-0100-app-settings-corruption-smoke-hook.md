<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0100: App Settings Corruption Smoke Hook

## Status

Accepted.

## Context

ADR-0094 stores Audio/MIDI setup preferences in app-level `settings.json`
outside project packages. The loader intentionally falls back to defaults when
the settings file is missing, invalid, or unsupported so a bad app-settings file
does not prevent the DAW from launching or corrupt the active project package.

Unit tests cover settings parsing and reset persistence, but the JUCE app also
needs app-boundary coverage that proves `MainComponent` can load from an
isolated malformed settings file and recover through the same command path used
by the UI.

## Decision

Add a hidden JUCE app command-line mode,
`--smoke-app-settings-corruption`, registered as
`projectname_app_settings_corruption_smoke` in CTest.

The smoke mode creates a temporary malformed `settings.json`, points
`MainComponent` at that isolated file, verifies default in-memory app settings
are restored without changing the active project package, then dispatches the
existing Audio/MIDI reset command. The test reloads the same isolated path to
verify valid, human-readable JSON was written.

## Consequences

- CI now covers settings corruption recovery at the app boundary without using
  the user's real application-data directory.
- The recovery path stays on the application/settings side and performs no
  real-time audio callback work.
- Project package state remains isolated from app settings recovery.
