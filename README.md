<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Rabbington Studio

Rabbington Studio is a free/open-source native desktop music production app. The
goal is a cross-platform DAW for Windows, macOS, and Linux with clip-based and
arrangement-based workflows, original built-in devices, and safe third-party
plugin hosting over time.

This repository currently contains the first vertical slice, not a complete DAW.

## License

Original Rabbington Studio source, documentation, and build scripts are licensed under
`AGPL-3.0-or-later`; see `LICENSE` and `docs/SOURCE_HEADER_POLICY.md`.
Third-party dependencies remain under their own licenses as recorded in
`docs/DEPENDENCIES.md`.

## What Exists

- C++20 + CMake native app scaffold.
- JUCE-based desktop app target.
- Dark DAW workspace skeleton with transport, browser, central workspace, device
  panel, mixer area, and inspector.
- Audio/MIDI device setup abstraction using JUCE's device manager.
- The Device Panel now shows a first-run Audio/MIDI setup prompt, ready/error
  output states, and a non-modal setup action that opens the existing JUCE
  selector without involving plugin scanning.
- App-level Audio/MIDI setup preferences persist outside project packages in a
  per-user `Rabbington Studio/settings.json`, including first-run prompt
  dismissal, a readable output summary, and JUCE restore state when available.
- Malformed or symlinked app settings, including broken settings links, fall
  back to defaults; core tests cover load/save symlink rejection, broken-link
  save rejection, and stale temporary symlink cleanup, including broken links,
  and a hidden smoke test verifies the Device Panel warning and recovery path
  that rewrites valid human-readable settings without touching projects.
- The Device Panel exposes a visible `Reset Prefs` action that clears the saved
  Audio/MIDI reminder dismissal and preferred output intent without touching
  project packages or the current audio callback.
- Play/Stop transport state with tempo, time signature, and timeline position.
- Minimal core audio engine stub that renders a generated test tone and a
  duration-bounded generated clip through the Windows fallback smoke path, plus
  sample-position scheduling for a generated timeline clip and prepared mono
  audio-buffer clips, including clip-local segment offsets for timeline seeks.
- Dependency-free PCM16 WAV import into a prepared mono buffer outside the audio
  render path.
- Project package model that writes a human-readable `manifest.json` plus
  `audio`, `samples`, `presets`, `analysis`, and `backups` folders, including
  tracks, clips, static track mix state, and device-chain placeholders. Saving
  rejects symlinked package paths, asset folders, or final manifest paths,
  including broken links, and loading rejects symlinked manifest paths,
  including broken links, instead of following them.
- Core project-package audio import that copies PCM16 WAV files into `audio/`,
  records `audio-file` clips with package-relative paths, and returns a prepared
  mono playback buffer.
- Audio import stages copied files before committing them into the project
  package and cleans up cancelled/failed staged imports.
- Imported PCM16 WAV clips get package-relative waveform summary metadata under
  `analysis/` for thumbnail drawing.
- Basic imported-waveform thumbnail state is loaded from package analysis
  metadata and drawn in the native workspace/fallback paths.
- The right inspector shows read-only metadata for the selected imported audio
  clip, falling back to the first imported clip, including package path,
  duration, source sample rate, and sample-rate mismatch warning when
  applicable.
- Missing or invalid imported-waveform analysis can be regenerated from package
  audio through a cancellable background job.
- Imported audio clips are arranged into a simple beat-scaled timeline lane
  layout with deterministic overlap rows and per-clip waveform states.
- The JUCE timeline lane can hit-test imported audio clip rectangles with the
  mouse, update persisted selection through the session command, refresh the
  inspector, and draw a subtle selected-clip outline.
- When the JUCE workspace has keyboard focus, left/right arrow shortcuts select
  the previous/next imported audio clip in timeline order and keep the inspector
  and selected outline synchronized.
- The focused JUCE workspace draws a subtle keyboard-focus outline that is
  distinct from the selected-clip outline.
- App/session timeline viewport state stores clamped view start and horizontal
  zoom scale, and the JUCE timeline lane uses that state when building clip
  layout options.
- When the JUCE workspace has keyboard focus, command/ctrl-left/right pans the
  timeline viewport and command/ctrl-up/down zooms the viewport, with status
  feedback after each command.
- The workspace subtitle shows the current timeline view start and horizontal
  beat scale, so keyboard pan/zoom changes remain visible after the status text
  moves on.
- Compact workspace controls can pan the timeline view, reset it to beat zero,
  fit imported clips, center the selected imported clip, and zoom the viewport
  in or out without adding global shortcuts.
- A plain C++ selected-clip centering helper computes a viewport start for the
  selected imported timeline clip while preserving the current zoom scale.
- Focused workspace shortcuts route through a small tested command router before
  the JUCE panel executes clip-selection or viewport callbacks.
- A small app command registry records stable ids, metadata, scopes, and
  enabled/disabled state for the existing top-bar actions without binding global
  shortcuts yet.
- Top-bar actions dispatch through those stable app command ids and return small
  command results while preserving the existing button layout and behavior.
- Imported audio clips can be placed by project/session command; imports choose
  a deterministic non-overlapping start beat unless an explicit start beat is
  requested.
- Imported audio clip selection is persisted in the project manifest and exposed
  through project/session commands; stale selections fall back in the inspector.
- Project loop-region state is persisted in the manifest with session commands
  to set and clear finite beat ranges.
- Session transport advancement wraps through enabled loop regions with
  deterministic exact-end and overshoot behavior.
- Enabled loop regions render as a subtle range highlight in the shared timeline
  lane and native fallback/JUCE workspace paths.
- Plain C++ imported-audio timeline playback planning maps clip start/length
  beats to sample ranges at the current tempo and produces prepared-buffer
  activation offsets for clip entry, seek, and loop-wrap positions.
- Pressing Play can schedule cached imported PCM16 WAV timeline clips through a
  prepared multi-voice window, so overlapping cached clips render through the
  stereo voice-summing path.
- Prepared multi-voice timeline playback reports imported-audio sample-rate
  mismatches in metadata and the app status line; resampling is intentionally
  not implemented yet.
- When required imported clips are not cached yet, pressing Play starts the
  background timeline preparation path, decodes the missing voice-window clips,
  and falls back to generated tone only when no imported timeline clip can be
  used.
- Recently imported timeline clips are cached in a small entry-count and
  sample-memory-bounded prepared mono buffer cache so Play can schedule several
  recent clips without decoding the copied package WAV again.
- Uncached imported timeline clips, such as clips in reopened projects, are
  prepared on a background job before being cached and scheduled, including all
  missing clips needed for a prepared overlapping voice window.
- Background timeline preparation reports phase/percent in the app status area
  and can be cancelled from the top bar before it schedules audio.
- Imported clip media replacement invalidates the matching prepared playback
  cache entry so stale audio is not reused.
- ADR-0060 defines the first undo/redo boundary for imported clip placement and
  media replacement before editable inspector or drag/drop UI is added.
- App/session imported clip placement edits now keep a small undo/redo history
  that records successful non-no-op start-beat changes and leaves media cache
  state untouched.
- App/session imported clip media replacement edits now undo and redo
  `relativePath`, `analysisPath`, and `lengthBeats` while invalidating prepared
  playback cache entries in both directions.
- App command metadata now includes imported clip edit undo/redo routing for the
  top undo or redo stack entry; visible undo/redo controls and shortcuts are
  still future work.
- ADR-0064 defines the editable imported clip inspector flow before visible
  text fields are added, including focus ownership, commit/cancel behavior,
  validation, and undo grouping.
- A plain C++ imported clip inspector edit draft model validates selected-clip
  start-beat edits and media relink metadata for visible controls that call
  app-session commit commands.
- The JUCE right inspector exposes a selected-clip-only start-beat text field;
  Return commits through the app session and Escape restores the draft's last
  committed value.
- ADR-0067 defines the selected-clip media relink chooser flow, including
  background WAV preparation, staged package writes, metadata validation, and
  non-destructive undo behavior.
- A plain C++ imported clip media relink preparation model stages PCM16 WAV
  replacements, produces draft-valid package-relative metadata, and cleans
  cancellation or stale-selection staging for visible chooser wiring.
- The JUCE right inspector exposes a selected-clip-only Relink button that opens
  a native WAV chooser, prepares staged media replacement on a cancellable
  background job, commits current-selection results, refreshes the
  timeline/inspector/undo state, and previews the refreshed clip cache.
- ADR-0071 defines imported media cleanup/restoration boundaries so package
  maintenance can protect undo/redo history, previous manifest backups, and
  in-flight staging before any destructive cleanup exists.
- A plain C++ imported media package inventory model reports current-manifest,
  previous-backup, and session-protected references, unreferenced candidates,
  missing or unsafe references, and stale staging directories without deleting
  files.
- ADR-0073 defines the package-local media quarantine and restore command
  contract, including restore-manifest contents, rollback rules, active-work
  exclusions, and no permanent deletion.
- A plain C++ package media quarantine restore-manifest model serializes,
  loads, and validates human-readable JSON for moved entries, skipped entries,
  partial-failure state, and restore-conflict state without moving files.
- A plain C++ package media quarantine preflight model turns safe inventory
  candidates into restore-manifest drafts and rejects active package work,
  unsafe or missing references, protected requested assets, empty plans, and
  duplicate destinations without moving files.
- A plain C++ package media quarantine command moves preflight-approved audio
  files, analysis files, and stale staging directories into
  `backups/media-trash/<cleanup-id>/`, commits `restore-manifest.json`, and
  rolls back already-moved items on failure when their original paths remain
  empty.
- A plain C++ package media quarantine restore command loads
  `restore-manifest.json`, restores all or selected moved entries when original
  package paths are empty, records occupied-original conflicts without
  overwriting active media, and persists updated manifest state through
  `restore-manifest.json.tmp`.
- A cancellable background package media cleanup job runs inventory, preflight,
  quarantine, and restore command paths off the UI/audio threads while reporting
  phase and percent progress for future package maintenance UI.
- A plain C++ package media cleanup status model maps inventory, preflight,
  quarantine, restore, conflict, partial-failure, and cancellation states to
  stable user-facing copy, severity, and browser/inspector/status-bar
  affordance ids.
- A plain C++ package media cleanup batch discovery model lists package-local
  restore manifests, validates cleanup ids and manifest paths before loading,
  attaches cleanup status output, and reports non-fatal discovery issues.
- A plain C++ package media maintenance view model combines inventory status,
  cleanup batch discovery, selected cleanup batch fallback, and restore action
  enablement for the future non-modal Package Maintenance UI.
- The JUCE browser panel shows the first read-only package media maintenance
  surface from a background snapshot: inventory status, cleanup batch rows,
  selected batch state, restore readiness, and discovery issue count.
- Package media maintenance cleanup batch rows are mouse selectable and
  keyboard selectable from the browser panel while cleanup/restore execution
  remains out of scope.
- The top bar has a compact Project menu with native New, Save As, and Open
  project-package choosers. The deterministic demo package remains the initial
  Save target for smoke tests, and failed Open attempts keep the current
  project loaded.
- A plain C++ Save As package relocation policy and copy command clone
  package-local `audio/`, `analysis/`, `samples/`, and `presets/` before the
  target manifest is saved, preserve explicit external references, and start
  target-package backups fresh. If the final manifest commit fails after a
  completed copy, the active project stays on the source package and copied
  target assets are left in the chosen package for recovery or manual cleanup;
  the app status makes that recovery boundary visible, and the Project menu can
  copy the kept target package path or run a manifest-only retry after the
  blocking target manifest path is fixed. Copy rejects symlinked source package
  folders, source asset entries, target package paths, or intermediate target
  parents, including broken source and target links, before target mutation, and
  retry refuses existing target manifests, non-regular manifest paths, and
  missing or symlinked copied assets without recopying, deleting, quarantining,
  or touching audio playback.
- Save As package asset copying runs on a cancellable background job with
  phase, file-count, and byte-count progress, plus a guarded top-bar Cancel
  Save affordance.
- The Package Maintenance browser surface has a guarded Restore affordance for
  selected restorable cleanup batches. Restore runs through the background
  package media cleanup job and stays disabled with visible reasons for
  non-restorable states.
- The Package Maintenance browser surface also has a guarded Clean affordance
  for current cleanup candidates. Cleanup/quarantine runs through the same
  background package media cleanup job and stays disabled for empty, missing,
  unsafe, scan-waiting, or active-work states.
- The Package Maintenance browser now shows selected cleanup batch detail rows
  with moved/restored/restorable counts, conflict/error counts, and
  package-relative path previews copied from the restore manifest.
- Conflict and partial-failure restore entries expose non-mutating review
  detail actions: package-relative paths can be copied from focused browser
  rows, and activating those rows reveals or copies the restore manifest path
  while repeat restore stays disabled for review-blocked batches.
- A hidden JUCE app restore-detail smoke test verifies focused review-row
  Command/Ctrl+C copy and restore-manifest activation fallback without starting
  cleanup or restore jobs, including distinct status copy for package-relative
  and restore-manifest fallback paths.
- Restore-entry selection in the Package Maintenance browser is keyboard
  reachable: Up/Down keeps cleanup-batch navigation, Tab/Shift-Tab moves browser
  row focus, Enter/Space activates the focused row, Command/Ctrl+A selects all
  restorable entries, and Escape clears the restore selection.
- A plain C++ track voice scheduler turns timeline clip plans, render windows,
  and track gain/pan/mute/solo snapshots into mixer-ready voice descriptors.
- Project track volume, pan, mute, and solo state is persisted and fed into
  app-session prepared voice-window scheduling.
- The app shell exposes a compact static mix strip for the first audio track
  with volume, pan, mute, and solo controls wired through app-session commands.
- `AudioEngineStub` can consume prebuilt track voice schedules and immutable
  prepared sample buffers to sum overlapping mono voices into stereo output with
  deterministic hard clipping.
- Project save creates `backups/manifest.previous.json` before overwriting an
  existing manifest, writes the next manifest through `manifest.json.tmp`, and
  removes the staged manifest if previous-backup creation fails. Stale temporary
  manifest symlinks, including broken links, are removed before writing, while
  asset-folder and final manifest symlinks, including broken links, are rejected
  before manifest staging, backup, or commit so the save path does not follow
  them.
- Project load rejects manifest directories and manifest symlinks, including
  broken links, before parsing them; session coverage keeps the active project
  unchanged and verifies manifest-link failures do not rewrite isolated app
  settings or create `manifest.json.tmp`.
- Shared app session that keeps UI playback state tied to the project-backed
  transport and preserves the current in-memory project when package saves or
  loads fail, including final manifest symlink and broken-symlink save
  rejections.
- Project menu, Import control, and deterministic initial
  `Rabbington Studio Demo.project` package in the user's documents folder.
- Import uses a native file chooser in the JUCE shell, runs project-package WAV
  import on a background job with frame-level decode progress, byte-level copy
  progress, and cancel state, then hands the prepared mono buffer to audio
  playback outside the render callback.
- Unit tests for transport state, project serialization, tone rendering, audio
  engine generated-tone/generated-clip/prepared-buffer playback, app settings
  serialization/reset and settings-path failure behavior, scheduled clip
  start offsets, imported clip playback planning/rendering, PCM16 WAV import,
  project-package audio import, imported timeline Play preparation/cache
  behavior, prepared-cache hit/miss/eviction/stale rejection, byte budgeting,
  and invalidation, imported clip inspector metadata, deterministic imported
  clip selection and keyboard traversal, cached prepared voice-window playback,
  sample-rate mismatch metadata propagation, static track mix command updates,
  background timeline voice-window preparation and cancelled/stale completion
  handling, timeline viewport state, fit-to-clips and selected-clip centering
  helpers, imported clip placement and media replacement undo/redo, imported
  clip edit undo/redo command routing, indicator formatting, and keyboard
  commands, imported clip inspector edit draft validation and cancel behavior,
  imported clip media relink preparation and cleanup behavior, background media
  relink preparation jobs, imported media package inventory classification,
  package media quarantine restore-manifest validation, preflight planning,
  file-moving rollback behavior, restore/conflict behavior, Save As package
  relocation/copy policy/background job progress and cancellation, and
  background cleanup job status propagation,
  workspace command routing, app command registry metadata, enablement, and
  dispatch results, timeline lane
  layout/hit-testing/placement/loop-range scaling, track voice scheduling
  policy, persisted track mix state, stereo prepared voice summing, loop-region
  persistence/advance behavior, and background import success, failure,
  progress, and cancel boundaries.
- Windows-only fallback launcher for local MinGW verification of launch and
  generated-clip output, including app-level project save/load and audio-import
  smoke testing.
- JUCE app `--smoke-test` launch mode registered with CTest for supported
  desktop toolchains.
- JUCE app `--smoke-project-choosers` mode registered with CTest for hidden,
  deterministic New/Open/Save As coverage, including cancelled selections,
  duplicate New targets, failed Open attempts, Save As no-copy, and
  occupied-target, final-manifest, and package-asset-copy paths.
- JUCE app `--smoke-audio-midi-reset` mode registered with CTest for hidden,
  deterministic Audio/MIDI preference reset coverage against an isolated
  temporary settings file.
- JUCE app `--smoke-app-settings-corruption` mode registered with CTest for
  hidden malformed and unsupported-version settings fallback, Device Panel
  warning, and reset recovery coverage.
- JUCE app `--smoke-restore-details` mode registered with CTest for hidden,
  deterministic Package Maintenance review-row copy/activation and status-copy
  coverage.
- CTest now includes an SPDX checker fixture target that proves comment-free
  `CMakePresets.json` vendor metadata is accepted while missing headers and
  stale or nonzero exception baselines fail.

## What Does Not Exist Yet

- AIFF/FLAC/MP3 import, automatic resampling, interactive waveform editing,
  imported-media cleanup/restoration UI, command-palette relink entries,
  timeline zoom/scroll, loop-region editing controls, or drag/drop clip
  placement.
- Resampling, time-stretching, streaming, multi-track mixer strips, metering,
  and automation.
- MIDI clips or piano roll editing.
- Plugin scanning or plugin hosting.
- Functional device chains or built-in instruments/effects beyond the generated
  tone proof.
- Full autosave scheduling or recovery UI beyond the last-good manifest backup.
- Export/render workflow.
- Bundled plugins, samples, presets, commercial sounds, or proprietary assets.

## Build

Install CMake 3.24+ and a C++20 compiler. On Windows, install Visual Studio
2026 or Visual Studio 2026 Build Tools with the Desktop development with C++
workload, then run:

```powershell
cmake --preset dev --fresh
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

On macOS and Linux, use the host-generator developer preset:

```bash
cmake --preset dev-host
cmake --build --preset dev-host
ctest --preset dev-host --output-on-failure
```

The Windows `dev` preset uses Visual Studio 2026, MSVC, and x64. Both
desktop-app presets fetch JUCE 8.0.13 and nlohmann/json 3.12.0 from their
official GitHub repositories. Their test suites include the app launch,
project-chooser, Audio/MIDI reset, app settings corruption, and restore-detail
smoke tests plus the SPDX fixture check, CI artifact contents fixture check, the
macOS artifact upload guard checks, and core unit tests. See `docs/BUILDING.md`
for platform notes and the local-JUCE option. Linux and macOS app CI
prerequisites are tracked separately in
`docs/LINUX_JUCE_APP_PREREQUISITES.md` and
`docs/MACOS_CI_PREREQUISITES.md`.

To run only the domain tests without building the JUCE desktop app:

```bash
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev --output-on-failure
```

GitHub Actions runs the Windows MSVC desktop app build, the Linux JUCE app
build/test smoke gate under Xvfb, the macOS JUCE app build/test gate on
`macos-15` with Xcode 16.4, and a Linux `core-dev` build/test job for
dependency-light second-host coverage. CI caches CMake FetchContent downloads
for JUCE and nlohmann/json outside the build tree; generated build outputs
remain untracked. After the Windows MSVC and Linux JUCE app tests pass, CI
uploads 7-day artifacts containing only the staged executable and first-party
license/dependency notes, including `SHA256SUMS.txt` for after-download
integrity checks. CI verifies the staged artifact allowlist and checksum file
before upload so caches, build intermediates, plugins, presets, samples, and
proprietary assets fail the package gate. `docs/BUILDING.md` documents where to
find the successful-run artifacts, their expected names, and launch caveats for
unsigned debug/smoke packages. The macOS CI job is build/test-only and defers
artifact upload, signing, notarization, and installer work under a CTest
guardrail tied to
`docs/adr/ADR-0106-macos-artifact-signing-policy.md`.

On Windows machines with MinGW but without a JUCE-supported compiler, the
fallback launcher can be verified with the non-JUCE fallback preset:

```powershell
cmake --preset win32-fallback -G "MinGW Makefiles"
cmake --build --preset win32-fallback
ctest --preset win32-fallback --output-on-failure
.\out\build\win32-fallback\projectname_win32.exe --smoke-test
.\out\build\win32-fallback\projectname_win32.exe --smoke-audio
.\out\build\win32-fallback\projectname_win32.exe --smoke-project
.\out\build\win32-fallback\projectname_win32.exe --smoke-import
```

## Run

After building, launch the `projectname` app target from the generated build
directory or your IDE. On Windows with the `dev` preset, the executable is
`out/build/dev-vs2026/projectname_artefacts/Debug/Rabbington Studio.exe`. Press
Play to hear the generated test tone or the first relevant imported PCM16 WAV
timeline clip, use Save/Open for the first project package round trip, and use
Audio/MIDI to open the device setup dialog.

For automation on supported desktop toolchains, run `projectname --smoke-test`
for a launch smoke check or use `ctest --preset dev --output-on-failure`.

## Documentation

- `GOAL.md` is the product and engineering north star.
- `AGENTS.md` contains repository rules for Codex.
- `docs/MILESTONES.md` defines sequencing.
- `docs/PLUGIN_POLICY.md` documents plugin/legal constraints.
- `docs/UX_PRINCIPLES.md` defines UX direction.
- `docs/PACKAGE_MEDIA_CLEANUP_UX.md` defines package media cleanup and restore
  entry points, status copy, conflicts, and recovery behavior.
- `docs/SOURCE_HEADER_POLICY.md` defines SPDX header expectations.
- `docs/SPDX_EXCEPTIONS.txt` records the current SPDX enforcement baseline.
- `docs/DEPENDENCIES.md` tracks dependency/license decisions.
- `docs/BUILDING.md` explains setup and verification.
- `docs/LINUX_JUCE_APP_PREREQUISITES.md` records the Linux desktop/audio system
  packages expected before adding Linux JUCE app CI.
- `docs/adr/ADR-0001-technology-stack.md` records the initial stack decision.
- `docs/adr/ADR-0003-win32-fallback-verification-app.md` scopes the fallback
  launcher.
- `docs/adr/ADR-0004-app-session-boundary.md` records the shared app-domain
  session boundary.
- `docs/adr/ADR-0005-core-audio-engine-stub.md` records the first core audio
  rendering boundary.
- `docs/adr/ADR-0006-project-device-chain-placeholders.md` records the first
  persisted device-chain placeholder model.
- `docs/adr/ADR-0007-project-manifest-backups.md` records last-good manifest
  backup behavior.
- `docs/adr/ADR-0008-project-license-strategy.md` records the project license
  strategy.
- `docs/adr/ADR-0009-scheduled-generated-clip-playback.md` records the first
  sample-position clip scheduling proof.
- `docs/adr/ADR-0010-prepared-mono-clip-buffer.md` records the first prepared
  audio-buffer playback proof.
- `docs/adr/ADR-0011-pcm16-wav-import-prepared-mono.md` records the first
  audio-file import proof.
- `docs/adr/ADR-0012-project-package-audio-import.md` records the first project
  package audio import boundary.
- `docs/adr/ADR-0013-native-audio-import-ui-handoff.md` records the first native
  audio import UI handoff.
- `docs/adr/ADR-0014-background-audio-import-jobs.md` records the background
  import job boundary.
- `docs/adr/ADR-0015-audio-import-progress-cancel-state.md` records import
  progress and cancellation state.
- `docs/adr/ADR-0016-staged-project-package-writes.md` records staged audio
  import and manifest write behavior.
- `docs/adr/ADR-0017-cancellable-wav-decode-progress.md` records frame-level
  WAV decode progress and cancellation.
- `docs/adr/ADR-0018-waveform-summary-analysis-metadata.md` records imported
  audio waveform summary metadata.
- `docs/adr/ADR-0019-imported-waveform-thumbnail-rendering.md` records the
  shared thumbnail loading and native rendering boundary.
- `docs/adr/ADR-0020-waveform-analysis-regeneration.md` records cancellable
  background regeneration for missing or invalid waveform summaries.
- `docs/adr/ADR-0021-timeline-clip-lane-layout.md` records imported audio clip
  lane scaling and overlap layout.
- `docs/adr/ADR-0022-imported-audio-clip-placement.md` records imported audio
  placement commands and import start-beat behavior.
- `docs/adr/ADR-0023-timeline-loop-region-state.md` records project/session
  loop-region state and persistence.
- `docs/adr/ADR-0024-loop-region-transport-advance.md` records session-domain
  loop wrapping during transport advance.
- `docs/adr/ADR-0025-loop-region-timeline-rendering.md` records timeline lane
  loop-range layout and native rendering.
- `docs/adr/ADR-0026-imported-clip-timeline-playback-plan.md` records the
  imported clip beat-to-sample playback planner.
- `docs/adr/ADR-0027-imported-timeline-play-command.md` records imported
  timeline playback from the app/session Play command.
- `docs/adr/ADR-0028-imported-playback-prepared-buffer-cache.md` records the
  first prepared-buffer cache for imported timeline playback.
- `docs/adr/ADR-0029-background-timeline-playback-preparation.md` records the
  background worker for imported timeline playback cache misses.
- `docs/adr/ADR-0030-imported-media-cache-invalidation.md` records the imported
  media replacement cache-invalidation boundary.
- `docs/adr/ADR-0031-bounded-imported-playback-cache.md` records the bounded
  multi-entry prepared playback cache.
- `docs/adr/ADR-0032-prepared-playback-cache-memory-budget.md` records the
  prepared playback cache memory budget.
- `docs/adr/ADR-0033-timeline-preparation-progress-cancel-ui.md` records the
  timeline preparation progress/cancel UI.
- `docs/adr/ADR-0034-track-voice-scheduling-and-mixing-boundary.md` records the
  track voice scheduling and mixing boundary.
- `docs/adr/ADR-0035-stereo-prepared-voice-summing.md` records the first stereo
  prepared voice summing path.
- `docs/adr/ADR-0036-session-prepared-voice-window-playback.md` records the
  app-session path for cached prepared voice-window playback.
- `docs/adr/ADR-0037-background-voice-window-preparation.md` records background
  preparation for missing voice-window buffers.
- `docs/adr/ADR-0038-persisted-track-mix-state.md` records persisted track
  volume, pan, mute, and solo state.
- `docs/adr/ADR-0039-static-track-mix-controls.md` records the first static
  mixer controls in the app shell.
- `docs/adr/ADR-0040-imported-audio-sample-rate-policy.md` records the v0.1
  imported-audio sample-rate mismatch warning policy.
- `docs/adr/ADR-0041-imported-clip-inspector.md` records the first read-only
  imported clip inspector.
- `docs/adr/ADR-0042-deterministic-imported-clip-selection.md` records
  persisted imported clip selection state.
- `docs/adr/ADR-0043-timeline-clip-hit-testing.md` records native timeline clip
  mouse selection and selected-outline rendering.
- `docs/adr/ADR-0044-timeline-clip-keyboard-selection.md` records focused
  workspace keyboard traversal for imported clip selection.
- `docs/adr/ADR-0045-workspace-keyboard-focus-feedback.md` records the visible
  workspace focus treatment and shortcut ownership boundary.
- `docs/adr/ADR-0046-timeline-viewport-state.md` records app/session timeline
  viewport state for future scroll and zoom controls.
- `docs/adr/ADR-0047-timeline-viewport-keyboard-controls.md` records focused
  workspace keyboard controls for timeline pan and zoom.
- `docs/adr/ADR-0048-workspace-command-routing-boundary.md` records the boundary
  between focused workspace commands and future app-global commands.
- `docs/adr/ADR-0049-workspace-command-router.md` records the tested local
  routing helper for focused workspace shortcuts.
- `docs/adr/ADR-0050-app-command-registry-design.md` records the planned
  app-level command registry shape for global shortcuts and command palette work.
- `docs/adr/ADR-0051-app-command-registry-skeleton.md` records the first tested
  app command metadata and enablement registry.
- `docs/adr/ADR-0052-top-bar-command-dispatch.md` records the first command-id
  dispatch path for existing top-bar actions.
- `docs/adr/ADR-0053-timeline-viewport-indicator.md` records the persistent
  workspace viewport indicator.
- `docs/adr/ADR-0054-compact-timeline-viewport-controls.md` records the first
  visible viewport reset and zoom controls.
- `docs/adr/ADR-0055-compact-timeline-pan-controls.md` records the visible
  left/right viewport pan controls.
- `docs/adr/ADR-0056-timeline-fit-to-clips-viewport-helper.md` records the
  tested model helper for fitting imported clips in the timeline viewport.
- `docs/adr/ADR-0057-compact-timeline-fit-control.md` records the visible
  compact workspace control for fitting imported timeline clips.
- `docs/adr/ADR-0058-selected-clip-timeline-centering-helper.md` records the
  tested model helper for centering the selected imported timeline clip.
- `docs/adr/ADR-0059-compact-selected-clip-center-control.md` records the
  visible compact workspace control for centering the selected imported clip.
- `docs/adr/ADR-0060-imported-clip-edit-undo-boundary.md` records the first
  undo/redo boundary for imported clip placement and media replacement.
- `docs/adr/ADR-0061-imported-clip-placement-undo-history.md` records the
  first app-session placement undo/redo history skeleton.
- `docs/adr/ADR-0062-imported-clip-media-replacement-undo-history.md` records
  the media replacement undo/redo history and cache invalidation behavior.
- `docs/adr/ADR-0063-imported-clip-edit-undo-command-routing.md` records the
  command routing path for top-of-stack imported clip edit undo/redo.
- `docs/adr/ADR-0064-imported-clip-inspector-editing-flow.md` records the
  editable inspector focus, commit/cancel, validation, and undo grouping design.
- `docs/adr/ADR-0065-imported-clip-inspector-edit-draft-model.md` records the
  plain C++ draft/validation model for future imported clip inspector controls.
- `docs/adr/ADR-0066-visible-imported-clip-start-beat-control.md` records the
  first visible selected-clip-only imported clip inspector edit control.
- `docs/adr/ADR-0067-imported-clip-media-relink-chooser-flow.md` records the
  native selected-clip media relink chooser flow design.
- `docs/adr/ADR-0068-imported-clip-media-relink-preparation-model.md` records
  the plain C++ relink preparation and staged commit boundary.
- `docs/adr/ADR-0069-visible-imported-clip-media-relink-control.md` records
  the visible selected-clip-only inspector relink control.
- `docs/adr/ADR-0070-background-media-relink-preparation-job.md` records the
  cancellable background media relink preparation boundary.
- `docs/adr/ADR-0071-imported-media-package-cleanup-restoration-design.md`
  records the non-destructive cleanup inventory and restoration policy.
- `docs/adr/ADR-0072-imported-media-package-inventory-model.md` records the
  read-only imported media package inventory model.
- `docs/adr/ADR-0073-package-local-media-quarantine-and-restore-commands.md`
  records package-local media quarantine and restore command design.
- `docs/adr/ADR-0074-package-media-quarantine-restore-manifest-model.md`
  records the package media quarantine restore-manifest model.
- `docs/adr/ADR-0075-package-media-quarantine-preflight-plan-model.md`
  records the package media quarantine preflight plan model.
- `docs/adr/ADR-0076-product-name-rabbington-studio.md` records the public
  product-name decision and internal rename boundary.
- `docs/adr/ADR-0077-package-media-quarantine-command.md` records the
  package media quarantine file-moving command.
- `docs/adr/ADR-0078-package-media-quarantine-restore-command.md` records the
  package media quarantine restore command.
- `docs/adr/ADR-0079-background-package-media-cleanup-job.md` records the
  cancellable background cleanup/restore job wrapper.
- `docs/adr/ADR-0080-package-media-cleanup-status-model.md` records the
  package media cleanup status mapping contract.
- `docs/adr/ADR-0081-package-media-cleanup-batch-discovery.md` records the
  package media cleanup batch discovery contract.
- `docs/adr/ADR-0082-package-media-maintenance-view-model.md` records the
  package media maintenance view-model contract.
- `docs/adr/ADR-0083-package-media-maintenance-status-surface.md` records the
  first visible read-only Package Maintenance status surface.
- `docs/adr/ADR-0084-package-media-maintenance-selection-controls.md` records
  Package Maintenance cleanup-batch selection controls.
- `docs/adr/ADR-0085-package-media-maintenance-restore-guard.md` records the
  guarded Package Maintenance restore affordance.
- `docs/adr/ADR-0086-package-media-maintenance-cleanup-guard.md` records the
  guarded Package Maintenance cleanup/quarantine affordance.
- `docs/adr/ADR-0087-package-media-maintenance-batch-detail-surface.md`
  records the read-only selected cleanup batch detail surface.
- `docs/adr/ADR-0088-ci-fetchcontent-cache-and-core-host-coverage.md` records
  CI FetchContent caching and second-host core verification.
- `docs/adr/ADR-0089-native-project-package-choosers.md` records the native
  project package chooser workflow and Save As asset-copy boundary.
- `docs/adr/ADR-0090-audio-midi-first-run-setup-flow.md` records the
  first-run Audio/MIDI setup prompt and non-modal recovery path.
- `docs/adr/ADR-0091-save-as-package-asset-copy-policy.md` records Save As
  package asset-copy policy and the original manifest-only relocation guard.
- `docs/adr/ADR-0092-save-as-package-asset-copy-command.md` records the Save As
  package asset-copy command and native Save As wiring.
- `docs/adr/ADR-0093-background-save-as-package-copy-job.md` records the
  cancellable background Save As package-copy job.
- `docs/adr/ADR-0094-audio-midi-setup-preferences.md` records the app-level
  Audio/MIDI settings file and restore boundary.
- `docs/adr/ADR-0095-package-maintenance-restore-keyboard-affordances.md`
  records Package Maintenance restore-entry keyboard focus and shortcuts.
- `docs/adr/ADR-0096-deterministic-project-chooser-smoke-hooks.md` records the
  hidden project chooser smoke path for successful, cancelled, and failed
  selections.
- `docs/adr/ADR-0097-audio-midi-preference-reset-command.md` records the
  visible Audio/MIDI preference reset command.
- `docs/adr/ADR-0098-package-maintenance-restore-detail-actions.md` records
  non-mutating restore review detail actions for conflict and partial-failure
  entries.
- `docs/adr/ADR-0099-cmake-presets-spdx-metadata.md` records the CMake presets
  SPDX metadata decision.
- `docs/adr/ADR-0100-app-settings-corruption-smoke-hook.md` records the hidden
  app settings corruption recovery smoke hook.
- `docs/adr/ADR-0101-save-as-failed-target-cleanup-policy.md` records the
  post-copy Save As manifest-failure cleanup/recovery policy.
- `docs/adr/ADR-0102-save-as-failed-target-copy-action.md` records the
  non-destructive Project menu copy action for kept failed Save As targets.
- `docs/adr/ADR-0103-macos-ci-prerequisites-and-deferral.md` records the macOS
  CI prerequisites and build/test-only first-job boundary.
- `docs/adr/ADR-0104-save-as-failed-target-retry-design.md` records the
  manifest-only retry command boundary and overwrite/conflict policy for
  kept failed Save As targets.
- `docs/adr/ADR-0105-macos-build-test-ci-job.md` records the enabled
  build/test-only macOS JUCE App CI job.
- `docs/adr/ADR-0106-macos-artifact-signing-policy.md` records the macOS
  artifact, signing, notarization, installer, and unsigned debug package policy.
- `docs/AUDIO_MIDI_SETUP_UX.md` documents the first-run device setup UX states.
- `docs/CI_ACTION_PIN_REVIEW.md` documents the scheduled review checklist for
  GitHub-maintained CI action major pins.
- `docs/issues/NEXT-ISSUES.md` lists the next five small tasks.
