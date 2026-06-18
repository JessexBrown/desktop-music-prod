# AGENTS.md

Codex must follow these instructions for this repository.

## Mission

Build <ProjectName>, a free/open-source, native desktop DAW for Windows, macOS, and Linux. Optimize for reliable audio, inspiring UX, legal cleanliness, and incremental delivery.

## Repository Rules

1. Read GOAL.md before major work.
2. Keep changes small and reviewable.
3. Do not implement broad, half-finished systems when a vertical slice can ship.
4. Do not copy proprietary DAW UI, icons, samples, presets, manuals, or brand names.
5. Do not bundle proprietary plugins or assets.
6. Add every dependency to docs/DEPENDENCIES.md with version, source, license, and why it is needed.
7. Add meaningful architecture decisions to docs/adr/.
8. Update README.md and docs/BUILDING.md when setup changes.
9. Prefer tests for engine, model, serialization, timing, plugin scanning, and rendering logic.
10. Never knowingly break real-time audio safety rules.

## Build and Test Expectations

Once the CMake scaffold exists, prefer these commands unless project docs say otherwise:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

If a platform-specific build is needed, document the exact command in docs/BUILDING.md.

## Real-Time Audio Safety

Audio callback code must not:
- allocate memory;
- block or lock;
- perform file/network I/O;
- call UI code;
- log;
- scan plugins;
- parse project files;
- create/destroy heavy objects.

Use prepared buffers, immutable snapshots, atomics, lock-free queues, background jobs, and explicit handoff points.

## UI/UX Expectations

The app should feel modern, fast, and musical.

Maintain:
- consistent spacing;
- responsive drag/drop;
- smooth meters/scrolling;
- accessible contrast;
- clear focus states;
- undoable edits;
- command palette direction;
- helpful empty states;
- no modal-heavy workflows unless unavoidable.

## Plugin Rules

- First support installed plugins through host formats; do not bundle commercial plugins.
- Treat plugin scanning as unsafe: background process/job, timeout, quarantine, blacklist.
- Plugin load failure must not corrupt a project.
- Save missing-plugin placeholders in project files.
- Plugin format work must update docs/PLUGIN_POLICY.md.

## Documentation Rules

Use this structure:

```text
README.md
GOAL.md
AGENTS.md
docs/
  BUILDING.md
  DEPENDENCIES.md
  MILESTONES.md
  PLUGIN_POLICY.md
  UX_PRINCIPLES.md
  adr/
    ADR-0001-technology-stack.md
```

## Done Means

A task is done when:
- code builds;
- relevant tests pass or inability to run is clearly reported;
- docs are updated;
- dependencies are recorded;
- any new risks are noted;
- the next small task is clear.
