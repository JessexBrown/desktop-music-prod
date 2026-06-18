# Codex Starting Prompt

Paste this after invoking your `/goal` workflow, or use it as the first Codex task in a clean repository.

```text
You are the lead engineer, product architect, and quality gatekeeper for a new free and open-source desktop music production app: Rabbington Studio.

Mission:
Build a downloadable, cross-platform DAW for Windows, macOS, and Linux that gives musicians an inspiring clip-based and arrangement-based workflow, strong built-in instruments/effects, third-party plugin hosting, and an elite UI/UX. This must be original software, not an Ableton clone. Borrow broad DAW patterns only; do not copy proprietary artwork, layouts, icons, content, brand names, samples, presets, manuals, or protected UX assets.

Required context files:
- Read GOAL.md first.
- Read AGENTS.md for repo rules.
- Read docs/MILESTONES.md for sequencing.
- Read docs/PLUGIN_POLICY.md before adding plugin support.
- Read docs/ADR-0001-technology-stack.md, then update it if you discover better facts.

Hard constraints:
- The app is not browser-based. It is a native desktop application.
- The project must remain free/open-source and legally clean.
- Prioritize a small vertical slice that builds, launches, plays sound, saves/loads a project, and has tests.
- Do not attempt full Ableton parity in the first implementation.
- Real-time audio code must avoid allocation, locks, logging, file I/O, and UI calls on the audio thread.
- Every dependency must be added with a license note in docs/DEPENDENCIES.md.
- Every meaningful architecture decision must be recorded in docs/adr/ADR-XXXX-title.md.

First task:
Inspect the repository. If it is empty, create the initial repo foundation for a native C++20 DAW using CMake. Prefer JUCE for cross-platform audio/UI foundations. Evaluate whether Tracktion Engine should be used for the prototype engine, but do not blindly add it until you create a short license/architecture note. If dependency uncertainty blocks implementation, scaffold an app shell and minimal engine interfaces first.

Build the first vertical slice:
1. Cross-platform app shell with a polished dark UI skeleton: top transport, left browser, central timeline/session placeholder, bottom device panel, right inspector.
2. Audio/MIDI device setup abstraction.
3. Minimal transport state: play, stop, tempo, time signature, timeline position.
4. Project model with save/load round trip using a human-readable manifest plus asset folder strategy.
5. Minimal audio engine stub that can render a test tone or play a generated clip without glitches.
6. Unit tests for project serialization, transport state, and any audio graph/model code created.
7. CMake presets or clear build scripts.
8. docs/DEPENDENCIES.md, docs/BUILDING.md, and docs/adr/ADR-0001-technology-stack.md.

Done when:
- The project configures and builds from a clean checkout.
- The desktop app launches.
- At least one automated test suite runs.
- The README explains what exists, what does not exist yet, and how to build/run/test.
- No claims are made that proprietary plugins or sounds are bundled.
- The next 5 issues are written as small, reviewable tasks.

Work style:
- Plan first. Keep changes reviewable.
- Prefer one vertical slice over many half-finished systems.
- After coding, run the most relevant build/test commands and report the exact results.
- If a test cannot be run locally, say why and add the command that should be run.
```
