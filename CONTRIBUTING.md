# Contributing to OmniDrop

OmniDrop is intentionally small at the core: one normalized action model, multiple frontends,
and adapters for concrete processors.

## Before changing code

Read `AGENTS.md` and `docs/ARCHITECTURE.md`. Treat action IDs, CLI command semantics, worker
protocol fields, settings keys, and future IPC/plugin schemas as compatibility contracts.

## Development setup

1. Install CMake 3.24+, a C++20 compiler, Qt 6.5+, and Python 3.10+.
2. Install worker dependencies with `python -m pip install -r python/requirements.txt`.
3. Configure and build with CMake.
4. Run CTest and the Python unit tests before opening a pull request.

## Adding an action

1. Add the stable action metadata to `ActionCatalog`.
2. Implement it behind an adapter or application service, not directly inside a Qt click handler.
3. Expose the same action ID through GUI and CLI semantics where appropriate.
4. Add runtime capability detection for optional dependencies.
5. Add tests that verify output, source-file preservation, and structured failures.
6. Update `docs/ACTIONS.md`.

Transform actions should be non-destructive by default.

## UI changes

Keep the desktop UI low-noise and black/white/gray by default. Use spacing, typography, and
hierarchy before decorative panels or strong color. Secondary explanations belong in tooltips or
secondary text; errors and required decisions must remain visible.

## Before opening an issue

Choose the [bug report](https://github.com/stloendays/OmniDrop-Alpha/issues/new/choose)
or feature request template. For bugs, include a reproducible sequence, OS,
OmniDrop version, file format, and sanitized error output. Do **not** attach
confidential documents, personal file paths, API keys or tokens.

For suspected vulnerabilities, follow [SECURITY.md](SECURITY.md), not public issues.

## Pull requests

Small, focused changes are easier to review and safer to merge while multiple
contributors work in parallel.

1. Start from the latest canonical `main`; create one branch for one concern.
2. Keep existing action IDs, CLI arguments, worker protocol and workflow
   `schema_version=1` compatible unless an explicit migration is agreed.
3. Add/update tests for user-visible behavior and verify original files stay
   unchanged for non-destructive actions.
4. Run relevant CMake/CTest and Python unit tests. Packaging/runtime changes
   also need the Windows portable-package smoke test in CI.
5. Update README, documentation or examples as appropriate. Document any new
   optional dependency and whether remote access requires consent.
6. Fill in the [PR checklist](.github/PULL_REQUEST_TEMPLATE.md) with evidence,
   known risks and the chosen integration base.

Documentation-only contributions, beginner-friendly usage examples and
reproducible Windows bug reports are welcome. Real sanitized screenshots are
helpful for UI changes; conceptual previews should be clearly labeled.
