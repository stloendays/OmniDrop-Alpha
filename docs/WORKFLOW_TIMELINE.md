# OmniDrop Workflow Jobs Event Timeline (v2.5)

A saved Workflow Job now keeps a bounded, local sequence of operational events.
It explains how a run progressed, whether a retry was requested, which DAG
nodes completed, and if an unclean exit was later recognized as interrupted.

## UI and CLI

- Open **Workflow... → Jobs**, then select a job. Its compact history appears
  in the **Selected job history** area below the list.
- CLI: `omnidrop-cli workflow events <job-id>`. This prints one JSON object
  with `schema_version`, `job_id`, `status`, `attempts` and `events`.

The same `WorkflowJobService::events(jobId)` method is used by both frontends.

## Event contract

Every event is a JSON object with `at` (UTC ISO-8601 timestamp), `event`
(stable event type) and `attempt` (run attempt number, zero when queued).
Additional optional fields include `node_id`,
`completed_operations`, and `total_operations`. Events intentionally **do
not** store input/output paths or raw worker payloads.

Current event types include:

- `job.queued`, `job.started`, `job.completed`, `job.failed`,
  `job.stopped`, `job.interrupted`
- `workflow.node_started`, `workflow.node_completed`,
  `workflow.action_completed`, `workflow.failed`, `workflow.stopped`

The persisted Jobs state remains `schema_version=1`. Older files created
before v2.5 have no `events` array and load with an empty event list.
The most recent **160** events per job are retained in the same atomic
Jobs JSON file. The 6 MiB overall storage limit remains enforced.
Events are ordered by observation, not a distributed clock.

## Limitations

- The timeline tracks job and local action milestones; it is not a debug log,
  process snapshot or guaranteed transactional event ledger.
- The current state persistence uses best-effort writes during worker
  execution; errors may leave an incomplete timeline even if outputs exist.
- No per-node checkpoint-based recovery or unattended autoretry yet.
- Privacy-sensitive input file paths remain in the separate saved job
  definitions/results, as documented in [Workflow Jobs](WORKFLOW_JOBS.md).
