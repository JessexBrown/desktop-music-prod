<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0013: Native Audio Import UI Handoff

## Status

Accepted for the v0.1 prototype.

## Context

ADR-0012 added core project-package audio import, but the native app shell still
needed a user-facing path to choose a WAV file and hand the imported samples to
playback. The app must keep all decoding, file I/O, allocation, and project
manifest writes outside the audio callback.

The current local Windows toolchain can build the Win32 fallback target but not
the JUCE app target, so the implementation also needs a dependency-light smoke
test that proves the same core import path without claiming full JUCE app
verification.

## Decision

Add an Import control to the JUCE app shell:

- use JUCE's native asynchronous `FileChooser` for source WAV selection;
- import into the deterministic prototype project package through a background
  import job, then apply the completed project through `AppSession`;
- copy the source file into package `audio/` storage through `ProjectAudioImport`;
- hand the returned prepared mono buffer to `AudioDeviceService`;
- pause the registered audio callback while swapping the prepared buffer into
  `AudioEngineStub`, then resume callback rendering;
- report unsupported files, cancelled imports, and sample-rate mismatch
  limitations in the status area.

Add `projectname_win32_import_smoke` for local MinGW verification. The fallback
smoke creates a tiny PCM16 WAV, imports it into a project package, renders the
prepared buffer through `AudioEngineStub`, reloads the manifest, and verifies the
copied asset exists.

## Consequences

- The JUCE app shell now has a native Import button and a first user-facing audio
  import path.
- The audio render callback remains free of allocation, locks, file I/O,
  manifest parsing/writing, and UI calls.
- Local verification can prove import behavior through the Win32 fallback even
  while the full JUCE app target waits on an MSVC toolchain.
- Import progress, cancellation state, and staged package writes are handled by
  ADR-0015 and ADR-0016.

## Follow-Ups

- Keep native import UI aligned with the background job in ADR-0014, ADR-0015,
  and ADR-0016.
- Replace the deterministic project package with native New/Open/Save As project
  choosers.
- Add visible timeline clips and waveform thumbnails for imported audio.
- Add sample-rate conversion before user-facing import is considered complete.
