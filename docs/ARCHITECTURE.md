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
  - ActionDescriptor
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

Example request:

```json
{"command":"run","action_id":"file.sha256","path":"C:/data/report.pdf"}
```

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

## File safety

Transforms are non-destructive by default. The worker allocates a unique sibling path or output
directory and leaves the source untouched. A future in-place action would require a distinct action
ID and explicit destructive semantics.

## Persistence

Recent files and Activity history use Qt's platform-native `QSettings` storage under the OmniDrop
application identity. These records are local UI state; they are not uploaded. Settings keys are
still treated as serialized contracts once a public release relies on them.
