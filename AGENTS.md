# AGENTS.md

Read this file before changing the repository.

## Product goal

OmniDrop is a local-first desktop file utility. A user drops a file and sees only relevant,
truthful actions. Core behavior must remain useful without an account, cloud service, or AI model.

## Mandatory pre-change procedure

1. Inspect current `main` HEAD and dirty state.
2. Inspect active PR/branch topology and CI.
3. Choose the newest actual dependency base.
4. Re-read every shared file immediately before modifying it.
5. Record base branch and base SHA in the PR.

## Stable contracts

Treat action IDs, CLI commands, worker/IPC schemas, serialized state, public headers, plugin IDs,
and updater/version semantics as stable contracts. Prefer additive compatibility.

Current protocol contract: Python worker protocol v1 in `docs/ARCHITECTURE.md`.

## Shared files

Root CMake, README, GUI entrypoints/controllers, workflow files, schemas, and version/release files
are high-conflict surfaces. Patch them minimally from current HEAD. Never restore a stale whole-file
snapshot over newer unrelated work.

## Branch rules

- No feature development directly on `main` once collaborative development begins.
- One coherent concern per branch.
- Use stacked PRs for true dependencies.
- Do not edit another active agent's branch without explicit handoff.
- Merge stacked PRs in dependency order.

## Architecture rules

- Domain and application behavior must not live only in Qt widgets or slots.
- GUI, CLI, automation, and future agent interfaces reuse the same action IDs and services.
- Python/FFmpeg/OCR/PDF runtimes are adapters, not the product architecture.
- Long-running operations must not block the Qt UI thread.
- An action must not be marked available unless its backend can actually run.

## UI rules

Default to minimalist black/white/gray with low visual noise. Prefer typography, spacing,
separators, and hierarchy over decorative cards. Reserve strong color for status and attention.
Secondary helper text may use tooltips, but blockers, errors, destructive consequences, and
required decisions must stay visible.

## Windows product baseline

Before calling OmniDrop complete, address applicable identity/icon/About, single-instance launch,
settings/state recovery, logs/diagnostics, notifications/history, recent files, search/shortcuts,
installer/uninstaller, update verification/rollback, and packaged-build smoke tests.

## Release rules

- `project(... VERSION ...)` in root `CMakeLists.txt` is the authoritative version until replaced by
  an explicitly documented version source.
- Stable releases only come from integrated canonical code.
- Never self-overwrite a development checkout containing `.git`.

## Definition of done

The capability works through its intended callable surfaces, existing contracts remain valid,
tests pass, shared files are reconciled, documentation is updated, and remaining dependencies are
explicit.
