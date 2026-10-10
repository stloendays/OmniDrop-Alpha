# Batch rename preview (read-only)

OmniDrop provides a **PowerRename-style dry run** for local filenames. It
displays the current and proposed filenames side by side and highlights
collisions **without ever applying a rename, move or overwrite**.

This is a deliberately limited, independent implementation for OmniDrop.
No PowerToys code, screenshots, binaries or UI assets are copied.

## Desktop usage

1. Select or drag one or more files into OmniDrop.
2. Choose **Tools → Batch rename preview...**.
3. Enter a **Find** pattern and optional **Replace with** text.
4. Optionally enable **Use regular expression**, **Ignore case** or
   **Include file extension**.
5. Review each filename's **Ready**, **Conflict**, or **Unchanged** status.
   Use **Choose files...** to adjust the selection.

The dialog has no Apply/Rename button in this phase. Closing it changes nothing.

## CLI usage

From the extracted Windows package (or a local developer build):

\`\`\`powershell
.\omnidrop-cli.exe rename-preview --find IMG_ --replace photo- C:\Photos\IMG_01.png C:\Photos\IMG_02.png
.\omnidrop-cli.exe rename-preview --regex --find '^(report)-(\d+)' --replace 'draft-\2' C:\Reports\report-17.pdf
.\omnidrop-cli.exe rename-preview --find .jpeg --replace .jpg --include-extension C:\Photos\image.jpeg
\`\`\`

The CLI prints structured JSON to stdout with:

- \`schema_version: 1\`, \`operation: "rename.preview"\`, \`preview_only: true\`,
  \`can_apply: false\`;
- \`input_count\`, \`proposed_change_count\`, \`ready_count\`,
  \`conflict_count\`;
- ordered \`rows[]\`: \`source_path\`, \`current_name\`, \`proposed_name\`,
  \`target_path\`, \`status\` and \`reason\`.

Exit codes: \`0\` for a valid conflict-free preview, \`3\` if any candidate
has a conflict, \`2\` for validation errors and \`1\` for invalid CLI syntax.
There is **no execution flag or hidden mutation**.

When invoked via an agent or script, inspect the JSON report rather than
inferring safety solely from an exit code.

## Conservative safety rules

- 1–128 explicitly selected existing regular files; no recursive folder walk.
- Reject duplicate selections, symbolic links, and missing inputs.
- Keep the last file extension unchanged unless \`--include-extension\` is
  supplied.
- Reject path separators, control characters, reserved Windows names, trailing
  spaces/dots and filenames longer than 255 UTF-16 code units.
- Detect case-only renames, existing targets and many-to-one collisions
  conservatively, including on case-sensitive operating systems.
- Never automatically resolve conflicts, switch names, delete, create a folder,
  or write a rename journal at preview time.
- Current/target **absolute paths appear in CLI output**; redact them before
  posting reports in public issues or sending them to external services.

The same \`RenamePreviewService::preview()\` application service is used by
the GUI and CLI. This is separate from the local workflow action allowlist:
workflows may not perform any rename in this release.

## Next step

A future rename **execution** phase will need explicit confirmation,
verified preconditions, collision-safe temporary names, an atomic/recoverable
journal, and tested undo/rollback semantics. A green preview is not itself
permission to rename files.
