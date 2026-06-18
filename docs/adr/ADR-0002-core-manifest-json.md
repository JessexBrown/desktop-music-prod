# ADR-0002: Core Project Manifest Serialization

## Status

Accepted for the v0.1 prototype.

## Context

The project model and transport state need automated tests early because save/load
correctness is a core DAW reliability requirement. The first app shell uses JUCE,
but JUCE does not support MinGW on Windows, and the local workspace currently has
only a MinGW compiler available.

Keeping all model serialization behind JUCE would make model tests require the
same full desktop/audio toolchain as the app. That is unnecessary coupling for
domain code and makes it harder to test project files in CI variants.

## Decision

Use nlohmann/json 3.12.0 for `projectname_core` project manifest serialization.

The core library remains plain C++20 for:
- transport state;
- generated tone rendering;
- project package save/load with track, clip, and device-chain placeholder data;
- deterministic previous-manifest backup on repeated saves;
- focused unit tests.

JUCE remains the app framework for the native GUI and audio/MIDI device layer.
The `AudioDeviceService` is compiled only with the JUCE app target.

## Consequences

- Project save/load tests can run without a JUCE-supported desktop compiler.
- The project manifest uses a structured JSON parser/writer instead of ad hoc
  string manipulation.
- The dependency ledger now includes nlohmann/json and its MIT license.
- The full desktop app still requires a JUCE-supported toolchain such as MSVC on
  Windows.
