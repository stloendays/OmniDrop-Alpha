# OmniDrop Workflow Folder Watch (v2.4)

Folder watching is **opt-in, local, non-recursive and session-only**.
It automatically turns new stable files into durable Workflow Jobs through
the existing WorkflowJobService. There is no startup autostart, background
daemon, remote upload, hidden scheduler or auto-install of AI models.

## Desktop

1. Open **Workflow...** and create or load a local workflow.
2. Click **Watch folder...** and explicitly choose an existing directory.
3. Existing files are baselined and **will not be processed**.
4. Drop a *new* file into that directory. After its size and last-modified
   timestamp have stayed unchanged, the app queues it as a new Workflow Job.
5. Progress and failures appear in the Workflow Builder and the Jobs tab.
6. Click **Stop Watching** to stop discovering files. An active job finishes
   safely. To also stop that job, choose **Stop after current file**.
7. Closing the Workflow Builder requires confirmation and stops monitoring.
   No watcher resumes automatically after restarting OmniDrop.

The workflow graph is frozen while watching. Stop the watcher to edit it.
The watcher does not monitor subfolders or follow symlinks.

## CLI (explicit foreground session)

    omnidrop-cli workflow watch workflows/examples/text-clean.omniworkflow.json C:/Incoming
    omnidrop-cli workflow watch workflows/examples/text-clean.omniworkflow.json C:/Incoming --max-files 5

The first line watches until interrupted; the second exits after five
successfully queued attempts (individual runs can still fail). Standard
output is newline-delimited JSON with watch.armed / watch.job_queued /
workflow progress / watch.job_finished / watch.stopped events.

The CLI watch is a foreground process. Terminal interruption may affect an
active child process differently across operating systems; the desktop
**Stop after current file** control is the recommended safe stop mechanism.

## Duplicate/loop protection

- The watcher ignores every file already in the directory when armed.
- It only considers newly created or renamed files after at least two scans.
- Files must have the same length and modification time for at least
  1,800 milliseconds, and their last write must be at least that old.
- Temporary downloads (*.tmp, *.part, *.crdownload, *.download, *.swp,
  *.lock, *~), symlinks and files larger than 256 MiB are not processed.
- The watcher keeps an in-memory list of handled source paths; rewriting
  a handled filename does not process it again during the same session.
- After the job finishes, generated intermediate and final output paths
  are explicitly excluded before scanning resumes. A processed input cannot
  recursively feed its own newly written sibling files back into the watch.
- Only one OmniDrop process may watch a given canonical folder at a time,
  enforced by a per-user, per-folder QLockFile.
- A maximum of 4,096 direct child files may be watched; the session stops
  with an explicit error if the directory grows beyond that bound.
- Queue storage is still limited to 100 retained records. Remove completed
  records (without deleting output files) to free space if needed.

If a workflow cannot run on the new file type (for example a PDF action on
an image) or an optional processor is missing, the rejected file is reported
and not retried automatically. Workflows requiring multi-file PDF merging
are not supported by one-file folder arrivals.

## Security and limitations

Folder watching invokes only local allowlisted workflow actions. Online
translation and arbitrary shell commands are not available in the workflow
schema. Jobs store selected input file paths locally, as documented in
[Workflow Jobs](WORKFLOW_JOBS.md).

The scanner does not promise that a separate writer could not mutate a file
after the stability test; files remain ordinary filesystem inputs. There
is no server-side transaction or snapshot of the incoming file bytes.
Users should finish writing/copying files before moving them into a watched
directory, preferably using a temporary filename followed by an atomic rename.

Folder sessions are deliberately not persisted or started automatically.
The next stage can add explicit trigger profiles with auditable state,
input fingerprints, restart deduplication, quiet hours, scheduling and
notifications after approval and end-to-end testing.
