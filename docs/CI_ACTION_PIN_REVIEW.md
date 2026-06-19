<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# CI Action Pin Review

Review GitHub-maintained action major pins on a scheduled basis, at least once
per quarter, and whenever GitHub announces action runtime deprecations.

This is a manual maintenance checklist. Do not add an automated updater until
the project has a stronger release branch and CI rollback policy.

## Current GitHub-Maintained Actions

| Workflow use | Current pin | Release page | Runtime manifest |
|---|---:|---|---|
| Repository checkout | `actions/checkout@v7` | https://github.com/actions/checkout/releases | https://github.com/actions/checkout/blob/v7/action.yml |
| FetchContent cache | `actions/cache@v5` | https://github.com/actions/cache/releases | https://github.com/actions/cache/blob/v5/action.yml |

## Review Checklist

- Open each action's release page and read the release notes for the current
  major and any newer major.
- Open each current or candidate major tag's `action.yml` and check
  `runs.using` for runtime changes such as Node.js major-version moves.
- Confirm input/output names used by `.github/workflows/ci.yml` remain
  compatible.
- Confirm the action license remains compatible with CI-only use and update
  `docs/DEPENDENCIES.md` if version, source, license, or notes change.
- Keep the workflow topology stable unless the maintenance task explicitly
  changes it: preserve `Windows MSVC App`, `Linux Core`, job-specific
  `FETCHCONTENT_BASE_DIR` values, cache paths, cache keys, and restore keys.
- Run the local presets that match CI before pushing:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
ctest --preset core-dev --output-on-failure
```

- After pushing, verify the GitHub Actions run passes both jobs.

## Update Policy

- Prefer one small reviewable commit for action pin changes.
- Do not change dependency cache behavior in the same commit unless the action
  release notes require it.
- If a new action major changes runtime, cache semantics, permissions, or
  token behavior, add or update an ADR before changing the workflow.
- Do not bundle action source, generated cache contents, or workflow artifacts
  into the repository.
