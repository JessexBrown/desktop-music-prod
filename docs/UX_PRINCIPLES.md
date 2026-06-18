# UX Principles

## North Star

The app should feel like a musical instrument: responsive, readable, forgiving, and satisfying.

## Core Interaction Principles

1. **Fast first sound** — on first launch, help the user hear sound quickly.
2. **Search beats hunting** — samples, plugins, commands, and settings should be searchable.
3. **Everything important is undoable** — editing should encourage exploration.
4. **Drag/drop should preview** — users should see what will happen before releasing.
5. **No silent failures** — audio, plugin, and file problems need clear recovery paths.
6. **Panels are flexible** — users should be able to focus on composing, mixing, editing, or performing.
7. **Keyboard workflow matters** — command palette and shortcuts are first-class.
8. **Make empty states useful** — every blank panel should teach the next action.
9. **Accessibility is design quality** — contrast, focus states, text scaling, and screen-reader naming matter.
10. **Audio must never suffer for visuals** — if CPU is tight, degrade nonessential visuals before audio.

## Default Layout

```text
Top: transport, tempo, grid, metronome, CPU, search/command palette
Left: browser for samples/plugins/presets/projects
Center: arrangement/session workspace
Right: inspector for selected track/clip/device
Bottom: device chain, piano roll, sample editor, automation editor
Lower/mixer: meters, sends, master, cue
```

## Visual Style Direction

- Dark-first, but support light/high-contrast themes later.
- Use consistent spacing tokens.
- Use strong typographic hierarchy.
- Use restrained animation tied to musical feedback.
- Meters should be smooth and readable.
- Clips should be colorable and legible at small sizes.
- Focus outlines must be visible.

## Microinteractions

- Transport buttons give immediate visual and audio-state feedback.
- Dragging clips snaps with subtle grid feedback.
- Dropping plugins into device chains shows insertion target.
- Recording state must be unmistakable.
- CPU overload should be visible but not panic-inducing.
- Missing files/plugins should show repair buttons.

## First-Run Experience

1. Welcome screen.
2. Audio device setup test.
3. MIDI device detection.
4. Choose template: Beat, Vocal Recording, Band Recording, Empty.
5. Open demo project.
6. Show three starter actions: add sample, add instrument, record audio.

## UX Acceptance Tests

- A first-time user can create a 4-bar beat without reading a manual.
- A returning user can open recent projects quickly.
- A plugin crash does not make the project feel lost.
- A missing sample can be relinked.
- Main commands are discoverable from the command palette.
- No critical feature depends only on color.
