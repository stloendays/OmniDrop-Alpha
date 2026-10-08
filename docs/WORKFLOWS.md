# OmniDrop Workflow v1

OmniDrop Workflow combines existing, offline Action IDs into repeatable
file-processing pipelines. Workflow definitions are versioned JSON files. The
same manifest runs in the Qt editor, CLI, and future agent adapters.

## Desktop quick start

1. Select or drop one or more local files into OmniDrop.
2. Click **Workflow...** or **Tools → Workflow builder...**.
3. Choose a recipe or add steps in order and move them up or down.
4. Click **Preview plan** to validate inputs, dependencies and available processors.
5. Click **Run workflow** and review generated files and per-node results.
6. **Save...** writes a reusable .omniworkflow.json file. **Load...** opens it.

The Qt editor supports two authoring modes:

- **Linear recipes:** add/remove/reorder steps in the list, then choose
  **Edit connections on graph** to convert them into an editable DAG.
- **Graph editor:** move node cards on the canvas; drag the circle on a node's
  right edge onto the left-edge circle of another node to connect. Right-click
  an existing link to disconnect it. Add new nodes as independent branches,
  or remove them and reconnect downstream inputs to Files. The editor detects
  dependency cycles and refuses invalid links.

Previously saved DAGs now load directly into editable graph mode, with the
same schema_version=1. Graph positions are intentionally transient in v2.2;
connections are saved in the JSON document, but visual coordinates are not.
A Graph and an Execution results tab keep editing separate from run evidence.

## CLI

    omnidrop-cli workflow validate workflows/examples/text-clean.omniworkflow.json
    omnidrop-cli workflow plan workflows/examples/text-clean.omniworkflow.json notes.txt
    omnidrop-cli workflow run workflows/examples/text-clean.omniworkflow.json notes.txt
    omnidrop-cli workflow run workflows/examples/image-webp.omniworkflow.json a.png b.jpg
    omnidrop-cli workflow run workflows/examples/pdf-merge-text.omniworkflow.json a.pdf b.pdf

Exit code 0 indicates success. Failed runs return nonzero and preserve a
structured JSON report with partial outputs for recovery.

## Workflow JSON schema (version 1)

    {
      "schema_version": 1,
      "name": "Clean and deduplicate text",
      "nodes": [
        {"id":"normalize","action_id":"text.normalize","sources":["$input"]},
        {"id":"unique","action_id":"text.deduplicate","sources":["normalize"]}
      ]
    }

- A node is a local action with a stable action_id.
- A source is either "$input" or the ID of another node.
- "$input" means files selected by the user, not paths embedded in the manifest.
- Multiple sources are concatenated in declaration order for branch joins.
- Each per-file action handles all incoming files independently.
- The batch action pdf.merge combines two or more PDFs into one file.
- Execution order is determined by dependencies, not JSON node order.
- Cycles, unknown/duplicate IDs and incompatible file types are rejected.
- Leaf nodes define the workflow outputs. Intermediate outputs are also
  recorded for inspection or failure recovery.
- file.sha256 outputs metadata only; pdf.split and archive.extract produce
  directories. These actions are terminal and cannot feed other file actions.

## Privacy, safety and bounds

- No shell command, external script, remote API call, secret, embedded file path,
  or unchecked plugin invocation is allowed inside workflow definitions.
- The action catalog is an explicit allowlist of existing local operations.
- Online translation remains available only through its separate explicit
  consent UI and CLI, never from a workflow without authorization.
- Piper TTS runs locally only when an optional verified voice pack exists.
- Inputs are never overwritten by built-in transforms.
- Preview is read-only and checks processor availability and type compatibility.
- Bounds: 64 KiB manifest, 24 nodes, 16 sources per node, 128 input files,
  and 1024 individual actions per execution.
- Execution remains sequential. The Workflow Builder now streams per-operation
  progress and offers **Stop after current file**. It never forcibly aborts a
  PDF write or voice-model inference in progress.
- Failed runs do not auto-delete intermediate outputs or claim rollback.
- The Qt application-service layer atomically writes saved manifests.

## Local worker protocol

The stateless worker v1 protocol adds three commands:

    {"command":"workflow.validate","workflow":{...}}
    {"command":"workflow.plan","workflow":{...},"paths":["C:/notes.txt"]}
    {"command":"workflow.run","workflow":{...},"paths":["C:/notes.txt"]}

Execution returns a run_id, each node status, created_output_paths,
leaf output_paths, and (on failure) a failed_node plus structured error.
Every step uses the same existing worker run/run_batch implementation.

No network listener is introduced. The first implementation contains no
background schedule, durable queue or automatic file watching.

## Streaming progress (opt-in)

The original `workflow.run` worker command and CLI continue to return a
single JSON object for compatibility. An additional
`workflow.run_stream` command accepts one JSON line on stdin and produces
newline-delimited JSON progress frames on stdout followed by one
`workflow.finished` frame containing the complete execution result.

GUI and CLI connect through the same C++ WorkflowService and
PythonWorkerClient. During streaming, a second stdin message
`{"command":"workflow.cancel"}` asks the worker to stop **after the active
file operation completes**. No intermediate output is automatically deleted.

`omnidrop-cli workflow run-stream <workflow.json> <file> [...]`
emits structured events and then a terminal result. Event fields include
schema_version, event, run_id, node_id, completed_operations,
total_operations, and status. File paths are not included in progress
frames. Final results still list created output paths.

## Next iterations

- Persistable node positions, keyboard-accessible link editing and canvas zoom
- Durable job IDs, failed-action retry, and crash/restart recovery
- Durable queue, restart recovery, triggers and monitoring
- Typed action parameters, variables and versioned migration
- Consent-aware online translation nodes
- Stable agent/MCP workflow gateway
