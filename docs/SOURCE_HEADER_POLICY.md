<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Source Header Policy

Rabbington Studio is licensed under `AGPL-3.0-or-later`.

## Policy

New source, build, script, and documentation files should include an SPDX license
identifier when the file format has a conventional comment syntax:

```text
SPDX-License-Identifier: AGPL-3.0-or-later
```

For C++ source and headers, use:

```cpp
// SPDX-License-Identifier: AGPL-3.0-or-later
```

For CMake files, shell scripts, Python scripts, Markdown documents, and YAML
files, use the comment form customary for that file type.

For first-party JSON files that do not support comments, prefer a valid metadata
field when the owning schema allows one. `CMakePresets.json` stores its SPDX
identifier under the top-level CMake presets `vendor` object so the file remains
valid for CMake while still being machine-checkable by `projectname_spdx_check`.

## Existing Files

Existing files may be updated gradually. Large mechanical header sweeps should be
reviewed separately from behavioral changes so code review remains focused.

`docs/SPDX_EXCEPTIONS.txt` records the current baseline of first-party files that
predate top-of-file SPDX enforcement. The CTest target `projectname_spdx_check`
fails when a first-party source, build, or documentation file lacks an SPDX
identifier and is not listed as a baseline exception.

## Third-Party Material

Do not add third-party source, binaries, samples, presets, fonts, icons, plugin
binaries, or generated assets without recording their license in
`docs/DEPENDENCIES.md` or a dedicated asset ledger. Third-party files must keep
their original notices.
