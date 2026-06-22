<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0001: Initial Technology Stack

## Status

Accepted for the v0.1 prototype.

## Context

The project needs a native, downloadable, cross-platform DAW foundation with
reliable audio, modern UI, project persistence, and a path toward plugin hosting.
The first vertical slice must stay small: launch a desktop app, show a DAW
workspace skeleton, play generated audio, save/load a project manifest, and run
tests.

The repository rules require every dependency to be reviewed for license impact.
The prototype also needs to remain legally clean and original, avoiding bundled
commercial plugins, samples, presets, icons, manuals, or protected product names.

## Decision

Use C++20, CMake, and JUCE 8.0.13 for the initial native app foundation.

JUCE is fetched from the official `juce-framework/JUCE` repository by CMake and
is used for:
- native application/window integration;
- cross-platform audio/MIDI device management;
- GUI widgets and painting.

Build a small `projectname_core` library for app-domain concepts:
- transport state;
- generated tone rendering;
- project package save/load;

Compile the JUCE audio device service with the desktop app target. Keep manifest
JSON serialization in the plain C++ core, as recorded in ADR-0002.

On Windows, the developer desktop-app preset currently targets the Visual Studio
2026 generator with MSVC and x64. The core-only and Win32 fallback presets remain
available for dependency-light local verification when JUCE is not being built.

CMake enables both `C` and `CXX` project languages. Rabbington Studio source
remains C++20, but JUCE's Linux app configure/generate path can introduce C
build rules through generated helper targets and platform checks; enabling C at
the top level keeps the Linux JUCE app preset generator-correct.

Do not add Tracktion Engine in the first implementation. Tracktion Engine remains
a candidate for a later prototype milestone, but it is not needed to prove the
first app shell/audio/project round trip. Deferring it keeps the dependency set
small and avoids locking early UI/model work to a high-level engine before the
project has reviewed its long-term fit.

## License Notes

JUCE 8.0.13 is dual-licensed under AGPLv3 or a commercial JUCE license. This
prototype uses the open-source AGPL path for original Rabbington Studio code, as
recorded in ADR-0008.

Tracktion Engine was reviewed but not added. Its repository license states a
dual GPLv3-or-later/commercial model. If added later, the project must re-check
license compatibility, contribution expectations, binary redistribution impact,
and whether the project license remains clear for public releases.

## Consequences

- The app is native, not browser-based.
- The first app build has a small dependency set: JUCE for native audio/UI and
  nlohmann/json for core manifest serialization.
- The UI and project model can develop without waiting on a full DAW engine.
- The generated tone path provides an audio callback proof without file I/O,
  logging, allocation, or UI calls inside the callback.
- Tracktion Engine integration can be evaluated as a small, explicit future task.

## Follow-Ups

- Add a repository license file before public distribution.
- Revisit Tracktion Engine during the plugin/timeline architecture milestone.
