# OmniDrop Workflow Jobs (v2.3)

Workflow jobs are a **local, durable execution queue**. They let you save a
workflow plus its input file selection, run it later, see its latest progress,
and explicitly retry a failed, stopped or interrupted attempt.

The queue is not a cloud service, scheduled task, or background daemon.
Jobs do **not** execute automatically after a restart. This is intentional
until folder-watch triggers and reliability controls have their own tests.

## Desktop

Open **Workflow...**. Create/load a workflow and choose inputs, then
click **Add to Jobs**. Open the **Jobs** tab to inspect status and progress.
Choose a saved job and click **Run / Retry**. You can still request
**Stop after current file** while it runs.

**Remove record** removes the saved job but does not delete source files,
intermediate results or generated outputs.

## CLI

    omnidrop-cli workflow enqueue workflows/examples/text-clean.omniworkflow.json notes.txt
    omnidrop-cli workflow jobs
    omnidrop-cli workflow execute <job-id>
    omnidrop-cli workflow retry <job-id>
    omnidrop-cli workflow remove <job-id>

Execute/retry streams newline-delimited events and a final result. Workflow
IDs are stable UUIDs. Job states are: queued, running, completed, failed,
stopped and interrupted.

## Retry semantics

**Retry reruns the entire workflow from its original input files.** The
current version does not skip successful nodes or replay an execution from a
partial output checkpoint. As each file transformation is non-destructive,
a retry creates uniquely named sibling output files and does not remove
previous results.

If a running application crashes, its lock disappears. The next Jobs
inspection identifies the orphaned running record as **interrupted**; it
will not silently execute it again. Select Run / Retry after reviewing
the source files and existing output files.

A completed job cannot be rerun under the same ID. Add a new job instead.

## Storage and privacy

The queue is stored in a versioned JSON file under the user's standard
application-data directory, at:

- Windows: local app-data / OmniDrop / workflows / jobs.json
- Linux: XDG data / OmniDrop / workflows / jobs.json

OMNIDROP_WORKFLOW_JOBS may override the location for development and
testing. CLI and GUI use this same location, independent of application
display names.

The store includes full local paths and workflow definitions, so it should
be treated as private user data. No job data is uploaded or collected in
telemetry. The application attempts owner-only filesystem permissions,
but does **not** claim encrypted storage.

Every state change uses QSaveFile's atomic write/replace and cross-process
QLockFile protection. A distinct run lock prevents the same job being
executed simultaneously in two processes. The store is limited to 100
job records and 6 MiB. Corrupt/unsupported files are rejected, never
silently replaced.

## Next

- Efficient recovery from verified per-file checkpoints and failed steps
- Persistent task event timeline and exportable support reports
- Safe folder-triggered jobs with stable-file/debounce and output-loop protection
- Optional run schedules, bounded concurrency and unattended-mode guardrails
