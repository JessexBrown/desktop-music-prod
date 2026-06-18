<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Audio/MIDI First-Run Setup UX

## Goal

Help a new Rabbington Studio user confirm a working audio output without making
startup feel blocked or modal-heavy.

## First-Run Flow

- Launch initializes the default output device through JUCE.
- The Device Panel shows an Audio/MIDI setup action for returning to the setup
  dialog at any time.
- On the first launch of a session, the Device Panel shows a dismissible setup
  prompt even if the default device opens successfully.
- Pressing the setup action opens the existing Audio/MIDI selector as a
  non-modal dialog.
- Pressing Play remains the fast output test path for the generated tone.

## Success States

- A ready output shows the current output name, sample rate, buffer size, and
  output-channel count.
- The first-run prompt can be dismissed for the current app session once the
  user is comfortable with the selected device.
- The top-bar Audio/MIDI command remains available after dismissal.

## Error And Unavailable States

- If initialization fails, the Device Panel keeps the setup action visible and
  shows the initialization error.
- If no output device is open or the open device has no output channels, the
  Device Panel reports output as unavailable.
- Error states do not replace the current project, stop project save/load, or
  launch plugin scanning.
- Recovery is always through the Audio/MIDI setup dialog, not a plugin browser,
  project chooser, or modal startup wizard.

## Explicit Boundary

This flow only configures audio/MIDI devices. It does not scan, validate, load,
download, or recommend plugins.
