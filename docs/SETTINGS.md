# Local preferences, backups and recovery

OmniDrop development builds provide a versioned, local-only preferences file
shared by the Qt desktop and `omnidrop-cli`. This feature is **not included
in the published v0.4.0 Alpha portable ZIP** until a later tested release.

## Scope and defaults

Settings schema version 1 contains only two preferences:

| Key | Default | Allowed values | Effect |
| --- | ---: | ---: | --- |
| `recent_files_limit` | 8 | 1–32 | GUI Recent Files visible count; new additions enforce the limit |
| `activity_history_limit` | 20 | 1–100 | GUI Activity visible count and retention after new actions |

Other persisted data is **not part of the settings backup**:

- existing Recent Files and Activity rows in Qt native `QSettings`
  (`history/recentFiles`, `history/actions`);
- Workflow Jobs, workflow manifests, transaction journals, results and files;
- tokens, API credentials, cached models, runtime binaries or other unknown keys.

The existing `QSettings` key names, contents and storage semantics remain
unchanged. Saving preferences does not clear the existing QSettings records.
Lowering a limit hides older entries immediately and can prune them when new
files/actions are recorded. Missing recent files can still be cleaned up by the
existing Recent Files loader.

## GUI

Choose **Tools > Preferences and backup...**.

- Edit both numeric limits and select **Save preferences** to atomically
  commit both at once.
- **Export...** creates a new JSON file; an existing file is never silently
  replaced.
- **Import...**, **Reset to defaults...**, and **Restore last good...**
  require explicit Yes confirmation. Invalid imports do not modify the live
  settings file.
- If the live configuration cannot be read, no default is silently written.
  Other file features continue with safe default limits while the dialog
  explains how to perform a confirmed recovery.

## CLI (same C++ service)

```powershell
.\omnidrop-cli.exe settings get
.\omnidrop-cli.exe settings set recent_files_limit 12
.\omnidrop-cli.exe settings set activity_history_limit 40
.\omnidrop-cli.exe settings export C:\Users\Me\Documents\OmniDrop-preferences.json
.\omnidrop-cli.exe settings import C:\Users\Me\Documents\OmniDrop-preferences.json --confirm
.\omnidrop-cli.exe settings reset --confirm
.\omnidrop-cli.exe settings recover --confirm
```

Commands return JSON with `schema_version: 1`, `operation`, `ok`,
`source`, `settings` and `error`. Success exit code is 0; validation or
filesystem failures return a nonzero code. A single-key `settings set`
preserves the other setting even when concurrent callers are active.

Paths passed to import/export must be absolute. Import accepts only small,
regular, non-symlink JSON files with known schema version and exact valid keys.
The maximum settings document size is 8 KiB.

## Atomic writes, rollback and corruption handling

The shared file is `OmniDrop/settings.json` inside Qt's
`QStandardPaths::GenericConfigLocation` (a per-user configuration location).
Both processes use this exact path, independently of their application names.
For isolated tests, `OMNIDROP_SETTINGS_PATH` may override it with an absolute
path. This is a testing/developer override, not a recommended end-user setup.

Each accepted update/import/reset takes a cross-process `QLockFile`, then:

1. Validate the current configuration. A damaged/unsupported file blocks
   updates to protect its evidence.
2. Atomically save the previous valid settings to
   `settings.json.good.json`, using `QSaveFile`.
3. Atomically write the new main JSON document.

The backup holds the **previous** valid value, not necessarily the latest value;
when preferences are first saved, the default values are checkpointed.

`settings recover --confirm` (or the GUI recovery button) verifies the
last-good document before restoring it. If the current file is corrupt,
recovery first preserves its bytes in a distinct
`settings.json.corrupt-<uuid>.json`. Oversized or symlinked damaged files are
not silently replaced. A missing or damaged backup returns a visible error;
there is no automatic destructive reset.

Exported JSON contains no file paths, historical filenames or tokens, because
only the two allowlisted preference keys are serialized. Users should still
review backup contents before distributing them.

## Compatibility and limitations

- No changes to worker protocol v1, Action IDs, existing CLI commands,
  Workflow JSON schema or native `QSettings` history keys.
- The preferences file is new; old builds simply ignore it. A future
  incompatible schema requires an explicit migration.
- This initial feature is preferences recovery, **not** backup of every
  user workspace, saved Job, credential or file. It does not automatically
  migrate settings from other computers.
- Backup and main settings are atomic individually, not an atomic two-file
  transaction. Recovery remains manual and visible.
