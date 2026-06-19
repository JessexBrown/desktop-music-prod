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
- New project package creation;
- Open project package loading;
- Save As no-copy completion;
- Save As package-asset-copy completion for package-local audio and analysis
  files.

The smoke path returns a non-zero application exit code when any step fails.

## Consequences

- CI can verify chooser-triggered package behavior without opening native
  dialogs.
- The real user workflow still uses native choosers.
- The app boundary now has coverage for successful chooser selections, while
  cancellation and failure-state coverage remains a small follow-up.
