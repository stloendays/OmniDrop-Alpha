# Architecture

## Dependency direction

```text
Presentation
  - Qt desktop GUI
  - CLI
  - future shell extension / IPC / agent gateway
        |
Application
  - OmniDropService
  - ActionCatalog
  - future ActionRunner / JobService
        |
Domain
  - FileKind
  - ActionDescriptor / ActionScope
        |
Adapters
  - PythonWorkerClient
  - RecentFilesStore / ActionHistoryStore
  - future FFmpeg / OCR / updater / platform adapters
```

The presentation layer may depend on application services. Domain and application logic must
not depend on concrete widgets. External tools and runtimes stay behind adapters.

## Stable contracts

The following surfaces are treated as contracts once published:

- action IDs, for example `file.sha256`;
- CLI command names and argument semantics;
- worker protocol fields;
- serialized settings/state keys;
- plugin/IPC schemas once introduced;
- release manifest and updater semantics.

Prefer additive extensions. Renaming an action label is presentation-only; renaming an action ID
is a contract change and requires a compatibility alias or explicit versioned migration.

## Action availability model

There are three distinct states:

1. **planned** - action metadata exists but no implementation has shipped;
2. **implemented but unavailable** - code exists, but its local processor/dependency or input format is not reachable;
3. **available** - both the implementation and runtime requirements are present for the current file.

`ActionCatalog` describes implemented/planned actions. `PythonWorkerClient` probes optional worker
capabilities, and `OmniDropService` intersects runtime capabilities with file-specific constraints.
The GUI and CLI therefore share the same availability semantics.

## Worker protocol v1

The Python worker is stateless. The host sends one JSON object on stdin and receives one JSON
object on stdout. This keeps lifecycle, UI state, history, and user-facing semantics owned by the
C++ host.

Per-file request:

```json
{"command":"run","action_id":"file.sha256","path":"C:/data/report.pdf"}
```

Ordered batch request:

```json
{"command":"run_batch","action_id":"pdf.merge","paths":["C:/data/a.pdf","C:/data/b.pdf"]}
```

Batch commands are additive protocol-v1 commands. A batch-scoped action consumes the full ordered
`paths` list as one operation; callers must not emulate it by independently invoking `run` for
each file.

Capability request:

```json
{"command":"capabilities"}
```

Success:

```json
{"ok":true,"action_id":"file.sha256","path":"C:/data/report.pdf","sha256":"..."}
```

Failure:

```json
{"ok":false,"error":{"code":"not_found","message":"File does not exist"}}
```

Future long-running jobs must move to a framed persistent protocol or local IPC transport with
explicit job IDs, progress events, cancellation, timeout, and crash recovery. Do not overload this
one-shot v1 protocol silently.

## Workflow engine contract v1

Workflow documents are independent .omniworkflow.json files with schema_version=1,
human-readable names, stable local action IDs, and node sources referencing
"$input" or preceding graph node IDs. Source order defines merge order, while
stable topological sorting defines execution order.

The worker v1 protocol gains three additive commands:

```json
{"command":"workflow.validate","workflow":{"schema_version":1,"name":"Example","nodes":[...]}}
{"command":"workflow.plan","workflow":{...},"paths":["C:/notes.txt"]}
{"command":"workflow.run","workflow":{...},"paths":["C:/notes.txt"]}
```

WorkflowService is the reusable C++ facade for Qt, CLI and later agent callers.
WorkflowEngine is the deterministic local Python executor, and dispatches only
allowlisted existing run/run_batch actions through the same worker handler.
Workflows may not include raw scripts, network commands or remote translations.
Preview verifies types/capabilities without writing files. Execution records
a run_id, each node status, final outputs and all created intermediate outputs.

Manifest saves are atomic in Qt. Running from the CLI does not automatically
persist a queue; durable monitoring and framed progress are future work.
See docs/WORKFLOWS.md for schema, limits and failure semantics.

## Optional translation worker command

Translation is an **explicit standalone command**, not an automatic file action.
The existing worker-v1 protocol accepts:

```json
{"command":"translate_file","path":"C:/notes.txt","source_lang":"en","target_lang":"zh","provider":"mymemory","allow_remote":true}
```

The C++ `TranslationService` validates input, consent, provider, extension and
language pair before sending requests to the local worker. A second validation
happens in the Python translation adapter. Cloud traffic is prohibited unless
`allow_remote=true`; no document names or paths are included in external
provider requests. Existing `run` actions and `capabilities.actions` semantics
remain unchanged. Model and credentials are not persisted in diagnostic logs.

## Verified optional offline model packs

Piper voice packs are separately distributed under
`models/voice-packs/<voice-id>/` with `manifest.json`, ONNX weights, native Piper
configuration, upstream MODEL_CARD and NOTICE. The worker validates
`manifest.sha256` and configuration structure before advertising
`text.to_speech` as available. The model inference runtime must separately
be installed; no background download, outbound request, file upload,
or inference on startup occurs. Existing `run` per-file protocol is reused.

## Desktop activation protocol v1

The desktop application uses a local, per-user Qt IPC endpoint for single-instance activation.
It does not open a TCP/UDP listener. A second launch forwards its file-open intent to the existing
process and exits instead of creating a second interactive instance.

Request:

```json
{"schema_version":1,"command":"app.activate","paths":["C:/data/a.pdf","C:/data/b.pdf"]}
```

Rules:

- `schema_version=1` and `command=app.activate` are stable local IPC contracts;
- `paths` preserves command-line order and may be empty when the intent is only to focus the app;
- only string path values are accepted;
- the local server uses the current-user access option;
- malformed or unknown activation messages are ignored rather than executed;
- after an unclean exit, a stale local endpoint may be removed only after connection retries fail.

The first instance owns file validation and normal application semantics after receiving the paths.
The activation channel is lifecycle IPC, not a general-purpose remote-control API.

## File safety

Transforms are non-destructive by default. The worker allocates a unique sibling path or output
directory and leaves the source untouched. A future in-place action would require a distinct action
ID and explicit destructive semantics.

## Persistence

Recent files and Activity history use Qt's platform-native `QSettings` storage under the OmniDrop
application identity. These records are local UI state; they are not uploaded. Settings keys are
still treated as serialized contracts once a public release relies on them.
