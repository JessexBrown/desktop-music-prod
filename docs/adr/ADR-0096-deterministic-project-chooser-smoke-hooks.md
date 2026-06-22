<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0096: Deterministic Project Chooser Smoke Hooks

## Status

Accepted.

## Context

The JUCE app uses native file choosers for New, Open, and Save As. Those dialogs
are correct for users but unsuitable for automated CI because they require
interactive desktop selection. The project still needs app-boundary coverage for
the package operations that chooser results trigger, especially Save As paths
that either save only a manifest or copy package-local assets before switching
packages.

## Decision

Add a hidden JUCE app command-line mode, `--smoke-project-choosers`, registered
as `projectname_project_chooser_smoke` in CTest.

The smoke mode does not expose UI controls. It creates deterministic temporary
package selections and calls the same chooser-selection helpers used by the
native `FileChooser` callbacks. The sequence covers:
- cancelled New, Save As, and Open selections;
- New project package creation;
- duplicate New project targets;
- Open project package loading;
- missing and invalid Open project selections;
- Save As no-copy completion;
- Save As package-asset-copy completion for package-local audio and analysis
  files.

Failure-state steps assert that the active project package remains unchanged,
that no native chooser object is left open, and that no Save As package-copy job
is left running.

The smoke path returns a non-zero application exit code when any step fails.

## Consequences

- CI can verify chooser-triggered package behavior without opening native
  dialogs.
- The real user workflow still uses native choosers.
- The app boundary now has coverage for successful, cancelled, and failed
  chooser selections.
