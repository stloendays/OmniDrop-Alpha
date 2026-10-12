# Safe rename transactions

OmniDrop's original **Batch rename preview** remains read-only until you
explicitly choose **Prepare & apply...** and confirm a prepared plan.
Verified same-folder rename transactions, CLI inspection, Undo and manual crash
recovery have shipped since **v0.4.0 Alpha**. The additional **Rename history
and recovery** desktop window is implemented in the current development source
and is **not part of the tagged v0.4.0 Windows ZIP**.

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

## GUI transaction history (development source)

Open **Tools > Rename history and recovery...** (also in **History**).
The dialog searches the local journal store, including records from prior
OmniDrop sessions. It displays the transaction ID, UTC creation time, state
and file count. Selecting a record opens its original/target file paths locally;
records with invalid journals are visible as invalid but cannot be executed.

Actions are enabled only after a valid, current journal has been read:

- **Apply prepared...** only for `prepared` plans;
- **Undo selected...** only for `committed` plans;
- **Recover interrupted...** only for `committing`, `undoing` or
  `recovery_required` plans.

Every operation requires a separate Yes confirmation with its transaction ID,
then calls the same C++ service as the CLI. Fingerprints, journal state and
occupied paths are checked again by the service on a worker thread. Errors and
recovery-required states are shown, never automatically retried. **Refresh**
reloads records changed by another process. Closing the dialog while it owns a
running operation is blocked. The history window does not auto-recover or
remove journals; it is not an automatic background watcher.

## CLI

After reviewing the on-screen preview, create a persistent plan:

```powershell
.\omnidrop-cli.exe rename-preview --find IMG_ --replace photo- C:\Photos\IMG_01.png C:\Photos\IMG_02.png
.\omnidrop-cli.exe rename-prepare --find IMG_ --replace photo- C:\Photos\IMG_01.png C:\Photos\IMG_02.png
```

The second command returns a `transaction_id`, state `prepared` and the exact
original/target paths. The CLI can locate retained IDs through `rename-list`,
which does not reveal private file paths. The development GUI also lists
historical transactions. Review the plan carefully before confirming:

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
`recovery_required` if a safe restoration is blocked. Full crash recovery requires explicit approval in the CLI or development GUI;
OmniDrop never automatically modifies filesystem state at startup.

## Compatibility

The existing `rename-preview` command, Action IDs, Python worker v1,
`.omniworkflow.json` schema, release packaging and existing Jobs stay
unchanged. The transaction commands are additive, and Windows/Linux CTest and
packaged CLI smoke tests are mandatory before merge.
