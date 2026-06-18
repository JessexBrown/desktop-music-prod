# Rabbington Studio GOAL.md

## Product Vision

Create a free and open-source, downloadable desktop DAW that helps musicians produce complete songs with a fast, inspiring workflow. The app should combine the immediacy of a clip launcher, the depth of a timeline arrangement view, modern built-in instruments/effects, and reliable third-party plugin hosting.

The product should feel professional, responsive, and fun: fast startup, smooth meters, satisfying drag-and-drop, strong keyboard shortcuts, instant visual feedback, undo everywhere, accessible color contrast, and no mystery states.

## Positioning

This is not an Ableton clone. It is an original open-source DAW inspired by common professional music-production patterns:

- Session/clip launching for experimentation.
- Arrangement timeline for song structure.
- Device chains for instruments and effects.
- Browser-first workflow for samples, plugins, presets, and project assets.
- Fast automation and modulation.
- Export/render workflows suitable for releasing music.

Avoid copying proprietary UI, icons, samples, manuals, names, color palettes, templates, presets, or trade dress from any commercial DAW.

## Target Users

1. Beginners who want a free tool that feels modern and approachable.
2. Beatmakers and electronic producers who need clips, MIDI, samples, synths, and effects.
3. Vocalists who need low-latency monitoring, recording, pitch correction, comping, and vocal-chain presets.
4. Open-source musicians who want Linux, Windows, and macOS support.
5. Developers who want a clean audio app codebase they can extend.

## Non-Goals for Early Versions

- No browser-first architecture.
- No attempt to bundle proprietary paid plugins such as Auto-Tune, Serum, Kontakt, Omnisphere, or commercial sample packs.
- No exact Ableton UI clone.
- No marketplace payments in v1.
- No mobile version in v1.
- No full surround/film post-production workflow in v1.
- No network collaboration until local projects are stable.
- No VST2 support unless the legal/licensing situation is explicitly resolved.

## Core Product Pillars

### 1. Reliable Audio First

The audio engine must be stable before the UI becomes fancy.

Requirements:
- No allocation, mutex locking, logging, file I/O, or UI work on the real-time audio thread.
- Deterministic transport behavior.
- Sample-accurate scheduling where practical.
- Plugin delay compensation.
- Offline render path.
- Clear recovery behavior when plugins crash or fail to scan.
- Performance tests for common projects.

### 2. Elite UX

The UI should feel like an instrument, not a form-based utility.

Requirements:
- Smooth transport, meters, scrolling, zooming, and drag/drop.
- Command palette.
- Search-first browser.
- Keyboard shortcut editor.
- Undo/redo across editing operations.
- High-DPI support.
- Accessible themes.
- Fast onboarding: demo project, templates, guided tooltips, first-run audio setup.
- Visual polish without sacrificing audio stability.

### 3. Plugin Friendly, Legally Clean

The app should host installed third-party plugins and ship a useful built-in device set.

Early priority:
- VST3 host support.
- Audio Unit host support on macOS when feasible.
- CLAP host support as a first-class open standard target.
- LV2 support, especially for Linux users.
- Plugin scanner with quarantine, blacklist, rescan, favorites, tags, and crash isolation plan.

The project may link users to popular free/open plugins, but must not bundle proprietary or uncleared binaries, presets, samples, logos, or trademarks.

### 4. Built-In Creative Devices

Ship useful built-ins so a new user can make music immediately:

MVP built-ins:
- Simple synthesizer.
- Drum sampler / drum rack.
- Audio sampler.
- EQ.
- Compressor.
- Limiter.
- Reverb.
- Delay.
- Gain/utility.
- Tuner.
- Metronome.

Later built-ins:
- Pitch correction / vocal tuning device with original name and UI.
- Wavetable synth.
- Granular sampler.
- Saturation/distortion suite.
- Multiband compressor.
- Convolution reverb.
- MIDI chord/scale tools.
- Arpeggiator.
- Modulators: LFO, envelope follower, step sequencer.

### 5. Community and Extensibility

Make the project attractive to contributors:
- Clear build instructions.
- Small first issues.
- Good architecture docs.
- Testable modules.
- Plugin/device SDK later.
- Preset/sample contribution policy.
- Code of conduct.
- Design contribution guidelines.

## Recommended Technical Strategy

### Default path: Native C++20 + CMake + JUCE foundation

Use C++20 for the core audio application and CMake for reproducible cross-platform builds. Use JUCE for cross-platform desktop application/audio/plugin-host foundations unless the project later decides on a custom engine or another framework.

### Engine decision

Codex must evaluate two paths and record a decision:

**Path A: Tracktion Engine prototype**
- Pros: DAW-like data model, timeline/audio/MIDI/plugin concepts, fastest path to a working prototype.
- Cons: separate licensing, separate contribution model, UI must still be built, long-term dependency risk.

**Path B: Custom engine on JUCE**
- Pros: full control, simpler contribution ownership, tailored architecture.
- Cons: much slower; high risk of spending months before a satisfying MVP.

Recommendation: build a thin app-domain interface around engine features so the UI and project model are not permanently locked to a single engine choice.

## MVP Definition

The MVP is not a full DAW. The MVP is the smallest lovable music-making loop:

1. Create/open/save a project.
2. Configure audio/MIDI devices.
3. Add tracks.
4. Add or record audio clips.
5. Add MIDI clips.
6. Play/stop/loop with tempo and grid.
7. Edit a clip in a simple piano roll or waveform view.
8. Mix tracks with volume, pan, mute, solo.
9. Load at least one third-party plugin format in an experimental plugin host path.
10. Use at least three built-in effects/instruments.
11. Export a stereo WAV.

## Acceptance Criteria for v0.1 Prototype

- App launches on at least one primary development OS.
- It can play generated audio reliably.
- It can save and load a project manifest.
- It includes a transport bar, browser placeholder, timeline/session placeholder, and mixer placeholder.
- It has unit tests for project serialization and transport state.
- README explains limitations honestly.
- CI runs build/tests on at least one OS.

## Acceptance Criteria for v0.2 Alpha

- Audio clip import and playback.
- Basic MIDI clip model.
- Basic piano roll editor.
- Track volume/pan/mute/solo.
- Timeline zoom/scroll.
- Undo/redo for main editing actions.
- Plugin scanning proof of concept.
- Crash/quarantine design for plugins.
- Export/render of a basic project.

## Acceptance Criteria for v0.3 Creative Alpha

- Session/clip launcher grid.
- Basic scene launch.
- Device chains per track.
- Built-in synth, drum rack, EQ, compressor, reverb, delay, limiter.
- Automation lanes.
- Project templates.
- First-run setup flow.
- Performance profiling.

## Definition of Done for Any Feature

A feature is done only when:
- It has a small product note or issue description.
- It has acceptance criteria.
- It builds on supported platforms or is guarded behind platform checks.
- It has tests where practical.
- It does not violate real-time audio constraints.
- It has no dependency/license surprises.
- It updates docs when behavior changes.
- It is reviewed for UX consistency.

## Real-Time Audio Rules

Never do these on the audio callback thread:
- Allocate memory.
- Lock mutexes.
- Wait on condition variables.
- Read/write files.
- Log to console or files.
- Touch UI components.
- Perform network requests.
- Scan plugins.
- Parse large JSON/XML.
- Create/destroy plugin instances unless proven safe and moved off-thread.

Use lock-free queues, immutable snapshots, preallocated buffers, atomics, and background jobs where needed.

## Project File Direction

Use a project package strategy:

```text
MySong.project/
  manifest.json
  audio/
  samples/
  presets/
  analysis/
  backups/
```

Rules:
- Manifest is human-readable.
- Asset paths are relative when inside the project package.
- External files are referenced safely with missing-file recovery.
- Autosave and backups are first-class.
- Project load should be resilient to missing plugins and samples.

## UI Layout Direction

Default workspace:

```text
+--------------------------------------------------------------------------------+
| Top Bar: transport | tempo | time signature | metronome | CPU | export | search |
+----------+------------------------------------------------------+--------------+
| Browser  | Session / Arrangement main workspace                 | Inspector    |
| samples  | clips, tracks, timeline, launcher, editors           | selected obj |
| plugins  |                                                      | properties   |
+----------+------------------------------------------------------+--------------+
| Device chain / piano roll / sample editor / automation detail                  |
+--------------------------------------------------------------------------------+
| Mixer strip area / meters / master                                             |
+--------------------------------------------------------------------------------+
```

UX qualities:
- Every major action should be possible by mouse and keyboard.
- Dragging should preview outcomes before commit.
- Errors should be recoverable and human-readable.
- Plugin problems should not destroy a session.
- Empty states should teach the next action.

## Suggested Names for Built-In Devices

Avoid protected names. Use original names such as:
- Pitch Lock — vocal pitch correction.
- Flux Synth — basic synth.
- Pulse Rack — drum rack.
- Spacewell — reverb.
- Tape Heat — saturation.
- CleanComp — compressor.
- Air EQ — equalizer.
- Delayline — delay.

## Success Metrics

Early success:
- New user can make a 4-bar loop in under 5 minutes.
- App starts quickly on a normal laptop.
- Audio playback is stable at practical buffer sizes.
- Project save/load is reliable.
- Contributors can build locally from documented steps.

Beta success:
- Users can produce and export complete songs.
- Plugin scanning is reliable and recoverable.
- Built-in devices are good enough for a complete starter project.
- UI feels polished, not experimental.
- Community issues are organized into small, achievable tasks.
