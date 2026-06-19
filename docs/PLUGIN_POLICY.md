<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Plugin Policy

## Goal

Support the plugin ecosystem users already have while keeping this open-source project legally clean and stable.

## What We Can Do

- Host plugins that users already installed on their own machines.
- Support open plugin standards where feasible: VST3, CLAP, LV2, and Audio Units on macOS.
- Ship original built-in devices.
- Link to recommended free/open plugins with license metadata.
- Provide a plugin browser, scanner, favorites, tags, and collections.

## What We Must Not Do Without Explicit Permission

- Bundle proprietary paid plugins.
- Bundle proprietary presets or sample packs.
- Use third-party logos or trademarks in a way that implies endorsement.
- Repackage plugins unless their license clearly allows redistribution.
- Call a built-in effect "Auto-Tune" or use another company’s protected product name.
- Download/install third-party binaries automatically without license review and user consent.

## Plugin Format Priority

### Phase 1

- VST3 host support.
- Audio Unit host support on macOS if supported by chosen framework.
- Internal built-in device API.

### Phase 2

- CLAP host support.
- LV2 support, especially on Linux.
- Plugin sandbox strategy.

### Phase 3

- Plugin compatibility database.
- User ratings/tags for local plugins.
- Optional open plugin catalog that links to official plugin pages.

## Plugin Scanner Requirements

A plugin scanner must:
- run off the UI thread;
- never run in the real-time audio callback;
- timeout and recover;
- quarantine crashing plugins;
- allow manual rescan;
- keep a log visible to users;
- store plugin metadata in a local database;
- preserve projects even when plugins are missing.

## Recommended Free/Open Plugin Catalog Candidates

These are candidates for a link-only catalog, not automatic bundling:

- Surge XT — open-source hybrid synthesizer.
- Vital — free tier; source has GPLv3 terms and trademark/preset restrictions that must be respected.
- Dragonfly Reverb — free reverb plugins.
- Dexed — open-source DX7-style synth.
- Helm — open-source synth.
- ChowDSP plugins — open-source effects.
- Airwindows — large set of free effects.

Before adding any catalog entry, create metadata:

```yaml
name:
official_url:
formats:
platforms:
license:
redistributable: true|false|unknown
notes:
last_verified:
```

## Built-In Pitch Correction Direction

Users want an Auto-Tune-like workflow, but the project must build its own original effect.

Product direction:
- Name: Pitch Lock or another original name.
- Modes: subtle correction, hard correction, scale lock, formant-safe mode if feasible.
- Workflow: detect key/scale, MIDI sidechain notes later, latency disclosure.
- UX: visual pitch trace, correction amount, retune speed, humanize, formant shift.

Implementation direction:
- Start offline/non-realtime for analysis if needed.
- Add realtime mode only after stable DSP profiling.
- Document latency.
- Avoid copying commercial plugin UI/branding.
