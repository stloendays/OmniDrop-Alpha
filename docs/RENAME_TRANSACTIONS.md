# Safe rename transactions (development)

OmniDrop's original **Batch rename preview** remains read-only and is the
default. A separate, explicitly confirmed transaction interface can commit a
verified rename plan and reverse it later. This feature is under integration
testing and is **not included in the v0.3.0 Windows Alpha Release**.

The transaction implementation is intentionally conservative:

- Existing regular files only, 1–128 selected files.
- Limit: 512 MiB total content hashed per transaction.
- Same-directory filename changes only. No cross-volume moves, directories,
  links, case-only renames, swaps or target replacement.
- A plan is an on-disk, schema-versioned JSON journal with original/target
  paths, size, last-modified time and SHA-256 file hashes.
- Each action requires an independent **explicit** confirmation. Preparing a
  plan does not modify any selected file.
- The application uses an exclusive, global per-user QLockFile to serialize its
  own rename transactions.
- Windows uses MoveFileExW without replace/copy flags. Linux uses
  renameat2(RENAME_NOREPLACE), failing closed if unavailable.
- Before moving a file, OmniDrop rehashes the source, checks its modified time
  and refuses occupied targets. **Other programs can still modify source files
  between verification and a native rename; keep input files closed and
  quiescent while executing a transaction.**
- An interrupted journal can be recovered by reconciling **actual** source
  and target files against stored hashes, not by trusting a step counter.
  Recovery never deletes or overwrites a path that another program created.
- A completed transaction's Undo refuses to move files if any target changed
  or an original name was reused, rather than overwriting newer user work.

## CLI

After reviewing the on-screen preview, create a persistent plan:

```powershell
.\omnidrop-cli.exe rename-preview --find IMG_ --replace photo- C:\Photos\IMG_01.png C:\Photos\IMG_02.png
.\omnidrop-cli.exe rename-prepare --find IMG_ --replace photo- C:\Photos\IMG_01.png C:\Photos\IMG_02.png
```

The second command returns a `transaction_id`, state `prepared` and the exact
original/target paths. A restarted GUI or CLI can locate retained IDs through
`rename-list`, which does not reveal private file paths. Review the plan
carefully before confirming:

```powershell
.\omnidrop-cli.exe rename-list
.\omnidrop-cli.exe rename-status <transaction-id>
.\omnidrop-cli.exe rename-apply <transaction-id> --confirm
.\omnidrop-cli.exe rename-undo <transaction-id> --confirm
```

If a process terminates unexpectedly during a transaction, inspect
`rename-status` and the file paths. Only for states `committing`, `undoing`
or `recovery_required`, restore the verified original filenames:

```powershell
.\omnidrop-cli.exe rename-recover <transaction-id> --confirm
```

Commands emit a JSON object with `schema_version: 1`,
`operation: "rename.transaction"`, `transaction_id`, `state`, `ok`,
`error` and affected path pairs. Nonzero exit codes indicate failures.
Unchanged files in a preview are excluded from the prepared rename operation.

**No background Jobs, watched folders, cloud operations or Workflow actions
automatically invoke these mutating commands.** Always review the transaction.

## Journal storage and privacy

The default path is the user's standard data directory under
`OmniDrop/rename-transactions`, or the explicit
`OMNIDROP_RENAME_JOURNAL_DIR` environment variable for isolated tests.

Individual journal records are written with QSaveFile's atomic replacement.
OmniDrop attempts owner-only filesystem permissions. Records contain absolute
file paths and SHA-256 digests; treat them as private local data and do not
upload them to public issue reports. The journal is not encrypted. Retention
is limited to 500 transactions and up to 1 MiB per transaction. Records are
not silently deleted by Undo/Recover so that failures remain inspectable.

A multi-file batch cannot be made wholly atomic using separate OS rename
calls: there is always a small interval between files. When errors occur,
OmniDrop attempts to restore verified originals and records
`recovery_required` if a safe restoration is blocked. Full crash recovery
requires an explicit command rather than automatically modifying filesystem
state at startup.

## Compatibility

The existing `rename-preview` command, Action IDs, Python worker v1,
`.omniworkflow.json` schema, release packaging and existing Jobs stay
unchanged. The transaction commands are additive, and Windows/Linux CTest and
packaged CLI smoke tests are mandatory before merge.
