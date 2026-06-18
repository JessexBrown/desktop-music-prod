<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# ADR-0008: Project License Strategy

## Status

Accepted for the v0.1 prototype.

## Context

The project must remain free/open-source and legally clean. ADR-0001 selected
JUCE 8.0.13 for the first native app foundation and assumed JUCE's open-source
AGPLv3 path unless maintainers chose a commercial JUCE license before
distribution.

The repository did not yet have a root license file or a source-header policy.

## Decision

License original ProjectName source, documentation, and build scripts under:

```text
AGPL-3.0-or-later
```

Add a root `LICENSE` file with the SPDX expression and a link to the canonical
Free Software Foundation license text. Add `docs/SOURCE_HEADER_POLICY.md` to
define how new files should carry SPDX identifiers.

Add a lightweight CTest check, `projectname_spdx_check`, that scans first-party
source, build, and documentation files. Existing files without a top-of-file SPDX
identifier are listed in `docs/SPDX_EXCEPTIONS.txt`; new files should carry the
identifier instead of expanding the exception list.

Keep third-party dependencies under their own licenses as recorded in
`docs/DEPENDENCIES.md`.

## Consequences

- The project now has an explicit free/open-source license compatible with the
  open-source JUCE licensing assumption.
- Contributors have a clear rule for new source-file SPDX headers.
- Any future move away from the AGPL path, such as buying a commercial JUCE
  license and relicensing original code, requires a new maintainer decision and
  ADR.
- Third-party plugins, samples, presets, logos, and binaries remain out of scope
  unless separately reviewed and documented.

## References

- GNU AGPLv3 text: https://www.gnu.org/licenses/agpl-3.0.txt
- SPDX identifier: https://spdx.org/licenses/AGPL-3.0-or-later.html
