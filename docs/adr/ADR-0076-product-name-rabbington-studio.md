<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0076: Product Name Rabbington Studio

## Status

Accepted for the v0.1 prototype.

## Context

The repository began with `ProjectName` as a placeholder while the first native
DAW vertical slice was established. The project now needs a real public product
name for documentation, application metadata, project manifests, and visible UI.

The codebase already uses internal names such as `projectname_core`,
`projectname_tests`, the `projectname` C++ namespace, `PROJECTNAME_*` CMake
options, and `.projectname-staging/` package folders. Renaming those identifiers
would touch most source files and risk mixing a mechanical rename with active
product work.

## Decision

Use **Rabbington Studio** as the public product name.

Apply the name to:
- README, goal, license, dependency, build, and issue documentation;
- JUCE product metadata and bundle identifier;
- app window/application display strings;
- project manifest `application` markers;
- deterministic demo package/project names.

Keep existing internal source namespace, target names, executable names, CMake
options, and hidden package staging folder names for now. A later dedicated
mechanical rename can revisit those identifiers if maintainers decide the churn
is worth it.

## Consequences

- User-visible surfaces now consistently identify the app as Rabbington Studio.
- Existing build commands and test target names remain stable.
- Historical ADRs and implementation identifiers may still mention
  `projectname_*` when referring to internal targets or namespaces.
