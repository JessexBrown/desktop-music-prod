# Milestones

## Milestone 0 — Repository Foundation

Goal: Make the project buildable and agent-friendly.

Deliverables:
- CMake project scaffold.
- App target.
- Test target.
- README.md.
- docs/BUILDING.md.
- docs/DEPENDENCIES.md.
- docs/adr/ADR-0001-technology-stack.md.
- Basic CI for at least one OS.

Acceptance:
- Clean checkout can configure, build, and run tests.

## Milestone 1 — App Shell + Audio Device Setup

Goal: Launch a native app with a professional DAW layout skeleton and working audio device initialization.

Deliverables:
- Transport bar.
- Browser panel.
- Main workspace placeholder.
- Device panel placeholder.
- Mixer/master placeholder.
- Audio/MIDI settings dialog.
- Generated test tone playback or equivalent safe audio proof.

Acceptance:
- User can launch app, select device, press play, and hear generated sound.

## Milestone 2 — Project Model + Save/Load

Goal: Create stable project persistence before advanced editing.

Deliverables:
- Project manifest model.
- Tracks, clips, tempo, time signature, device-chain placeholders.
- Save/load round-trip tests.
- Autosave/backups design.

Acceptance:
- A project can be created, saved, closed, reopened, and compared to original state in tests.

## Milestone 3 — Timeline Audio Clip Playback

Goal: Make a real music-making loop possible with audio clips.

Deliverables:
- Import WAV/AIFF/FLAC if supported by chosen stack.
- Track lanes.
- Clip placement.
- Loop region.
- Play/stop/seek.
- Basic waveform thumbnail.

Acceptance:
- User can import an audio file, place it on a track, play from timeline, and save/reopen.

## Milestone 4 — MIDI Clips + Piano Roll

Goal: Enable beat/melody creation.

Deliverables:
- MIDI clip model.
- Piano roll notes.
- Quantize basics.
- Velocity editing.
- Simple built-in synth playback.

Acceptance:
- User can draw a melody and hear it through the built-in synth.

## Milestone 5 — Mixer + Built-In Effects

Goal: Make tracks sound better.

Deliverables:
- Volume/pan/mute/solo.
- Track meters.
- Master meter.
- Device chain UI.
- Built-in EQ, compressor, reverb, delay, limiter, gain.

Acceptance:
- User can add built-in devices to tracks and export a processed stereo WAV.

## Milestone 6 — Plugin Host Prototype

Goal: Load installed third-party plugins safely.

Deliverables:
- Plugin scanner service/job.
- Plugin database.
- Quarantine/blacklist design.
- Plugin browser entries.
- Load one format behind an experimental flag.

Acceptance:
- A known test plugin can be scanned, inserted, saved, reopened, and bypassed.

## Milestone 7 — Session / Clip Launcher

Goal: Deliver the core creative differentiator.

Deliverables:
- Clip launcher grid.
- Scene launch.
- Clip launch quantization.
- Stop buttons.
- Record/overdub direction.

Acceptance:
- User can trigger clips live and record/arrange the result or export a loop.

## Milestone 8 — Creative Alpha

Goal: Make the app lovable.

Deliverables:
- Drum rack.
- Sampler.
- Better browser.
- Preset system.
- Command palette.
- Shortcut editor.
- First-run tutorial project.
- Theme polish.

Acceptance:
- A new user can make and export a short beat without installing anything else.

## Milestone 9 — Beta Hardening

Goal: Prepare for public users.

Deliverables:
- Crash reports or local diagnostic bundles.
- Plugin crash isolation improvements.
- Performance profiling.
- Accessibility pass.
- Installer/signing/release workflow.
- Documentation site.
- Contributor guide.

Acceptance:
- Public beta can be installed and used on Windows, macOS, and Linux with known limitations documented.
