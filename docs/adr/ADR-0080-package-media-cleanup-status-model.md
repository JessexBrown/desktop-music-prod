<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0080: Package Media Cleanup Status Model

## Status

Accepted for the v0.1 prototype.

## Context

Package media cleanup now has read-only inventory, quarantine preflight,
package-local quarantine, restore, and background job primitives. The visible
Package Maintenance UI still needs stable user-facing status copy and affordance
targets before browser, inspector, or status-bar controls can consume those
primitives.

The status mapping must keep the UI simple without moving file safety decisions
out of the tested cleanup commands.

## Decision

Add `PackageMediaCleanupStatus` to `projectname_core` as a plain C++ mapper.

The mapper converts existing inventory, preflight, quarantine, restore,
restore-manifest, background-progress, and background-result states into:

- a stable status kind;
- a severity;
- short status text;
- optional detail text;
- browser, inspector, and status-bar affordance ids.

The mapper does not create JUCE components, scan plugins, move files, define
retention policy, permanently delete quarantined media, or run on the real-time
audio callback. It is a UI contract over existing model/command state, not a
new cleanup authority.

## Consequences

- Future Package Maintenance UI can use one tested vocabulary for non-modal
  cleanup and restore messages.
- Browser, inspector, and status-bar surfaces can stay loosely coupled by
  consuming stable affordance ids.
- Cleanup command tests remain the source of truth for file safety; status
  tests cover presentation decisions.
- No dependency is added.

## Follow-Ups

- Add package-local cleanup batch discovery for restore manifests.
- Wire the status model into a visible Package Maintenance browser/status
  surface.
- Define retention rules before any permanent deletion command.
