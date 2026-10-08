"""OmniDrop Workflow v1: bounded, offline, dependency-aware file pipelines.

Workflows reference only already-supported local Action IDs. They never run
arbitrary shell/Python scripts, invoke online translation, or delete source
files. Pure graph validation and preflight are side-effect free.

Worker action execution is injected so GUI and CLI use the exact same worker
adapter as single-file and batch actions.
"""

from __future__ import annotations

import os
import re
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

SCHEMA_VERSION = 1
MAX_NODES = 24
MAX_INPUTS = 128
MAX_INVOCATIONS = 1024
MAX_SOURCES = 16
MAX_MANIFEST_BYTES = 64 * 1024
NODE_ID = re.compile(r"^[a-z][a-z0-9_-]{0,31}$")

TEXT = frozenset({
    ".txt", ".md", ".markdown", ".srt", ".vtt", ".rtf", ".csv",
    ".tsv", ".log", ".json", ".xml", ".yaml", ".yml", ".toml",
    ".ini", ".cpp", ".c", ".h", ".hpp", ".py", ".js", ".ts",
    ".html", ".css", ".sql",
})
SPEECH = frozenset({".txt", ".md", ".markdown", ".srt", ".vtt"})
COMPRESSIBLE = frozenset({".png", ".jpg", ".jpeg", ".webp"})
WEBP_INPUTS = frozenset({".png", ".jpg", ".jpeg", ".bmp", ".webp"})
ROTATABLE = frozenset({".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff"})


@dataclass(frozen=True)
class ActionSpec:
    scope: str  # "file" or "batch"
    suffixes: frozenset[str] | None
    output: str  # "same", "file", "directory" or "none"
    extension: str = ""


# Explicit allowlist of the existing, deterministic LOCAL worker Action IDs.
# Online translation, arbitrary commands, plugins, and executable scripts are
# intentionally not representable by a workflow manifest.
ACTIONS: dict[str, ActionSpec] = {
    "file.sha256": ActionSpec("file", None, "none"),
    "text.normalize": ActionSpec("file", TEXT, "same"),
    "text.deduplicate": ActionSpec("file", TEXT, "same"),
    "text.format_json": ActionSpec("file", frozenset({".json"}), "file", ".json"),
    "text.format_xml": ActionSpec("file", frozenset({".xml"}), "file", ".xml"),
    "text.to_speech": ActionSpec("file", SPEECH, "file", ".wav"),
    "image.compress": ActionSpec("file", COMPRESSIBLE, "same"),
    "image.convert_webp": ActionSpec("file", WEBP_INPUTS, "file", ".webp"),
    "image.rotate_clockwise": ActionSpec("file", ROTATABLE, "same"),
    "image.rotate_counterclockwise": ActionSpec("file", ROTATABLE, "same"),
    "image.resize_half": ActionSpec("file", ROTATABLE, "same"),
    "image.remove_metadata": ActionSpec("file", COMPRESSIBLE, "same"),
    "pdf.extract_text": ActionSpec("file", frozenset({".pdf"}), "file", ".txt"),
    "pdf.split": ActionSpec("file", frozenset({".pdf"}), "directory"),
    "pdf.rotate_clockwise": ActionSpec("file", frozenset({".pdf"}), "file", ".pdf"),
    "pdf.rotate_counterclockwise": ActionSpec("file", frozenset({".pdf"}), "file", ".pdf"),
    "pdf.merge": ActionSpec("batch", frozenset({".pdf"}), "file", ".pdf"),
    "archive.inspect": ActionSpec("file", frozenset({".zip"}), "file", ".txt"),
    "archive.extract": ActionSpec("file", frozenset({".zip"}), "directory"),
}


class WorkflowError(Exception):
    def __init__(self, code: str, message: str, node_id: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.node_id = node_id


@dataclass(frozen=True)
class Node:
    id: str
    action_id: str
    sources: tuple[str, ...]


@dataclass(frozen=True)
class Artifact:
    suffix: str
    is_file: bool = True


def _error(code: str, message: str, node_id: str = "") -> WorkflowError:
    return WorkflowError(code, message, node_id)


def _parse(document: Any) -> tuple[str, list[Node]]:
    if not isinstance(document, dict):
        raise _error("invalid_workflow", "Workflow must be a JSON object.")
    if len(str(document).encode("utf-8")) > MAX_MANIFEST_BYTES:
        raise _error("workflow_too_large", "Workflow manifest exceeds 64 KB.")
    if document.get("schema_version") != SCHEMA_VERSION or type(document.get("schema_version")) is not int:
        raise _error("invalid_schema", "Workflow schema_version must be 1.")
    if set(document) - {"schema_version", "name", "description", "nodes"}:
        raise _error("invalid_workflow", "Workflow contains unsupported top-level fields.")
    name = document.get("name")
    if not isinstance(name, str) or not 1 <= len(name.strip()) <= 100:
        raise _error("invalid_workflow", "Workflow name must contain 1-100 characters.")

    raw_nodes = document.get("nodes")
    if not isinstance(raw_nodes, list) or not 1 <= len(raw_nodes) <= MAX_NODES:
        raise _error("invalid_workflow", "Workflow must contain between 1 and 24 nodes.")

    ids: set[str] = set()
    nodes: list[Node] = []
    for raw in raw_nodes:
        if not isinstance(raw, dict) or set(raw) != {"id", "action_id", "sources"}:
            raise _error("invalid_node", "Each node requires id, action_id and sources only.")
        node_id, action_id, sources = raw["id"], raw["action_id"], raw["sources"]
        if not isinstance(node_id, str) or not NODE_ID.fullmatch(node_id):
            raise _error("invalid_node", "Node IDs must be 1-32 lowercase ASCII letters/digits/_/-.")
        if node_id in ids:
            raise _error("duplicate_node", f"Duplicate node ID: {node_id}.", node_id)
        ids.add(node_id)

        if not isinstance(action_id, str) or action_id not in ACTIONS:
            raise _error("unsupported_action", f"Local workflow action not supported: {action_id}.", node_id)
        if not isinstance(sources, list) or not 1 <= len(sources) <= MAX_SOURCES:
            raise _error("invalid_node", "Each node requires 1-16 input sources.", node_id)
        if not all(isinstance(s, str) and s and s != node_id for s in sources):
            raise _error("invalid_source", f"Invalid source in node {node_id}.", node_id)
        if len(set(sources)) != len(sources):
            raise _error("invalid_source", f"Duplicate source in node {node_id}.", node_id)
        nodes.append(Node(node_id, action_id, tuple(sources)))

    for node in nodes:
        for source in node.sources:
            if source != "$input" and source not in ids:
                raise _error("unknown_source", f"Node {node.id} references unknown source {source}.", node.id)

    # Stable Kahn topological sort, preserving source JSON order among ready nodes.
    ordered: list[Node] = []
    pending = list(nodes)
    completed: set[str] = set()
    while pending:
        ready = next(
            (node for node in pending if all(source == "$input" or source in completed
                                             for source in node.sources)),
            None,
        )
        if ready is None:
            raise _error("dependency_cycle", "Workflow contains a dependency cycle.")
        ordered.append(ready)
        completed.add(ready.id)
        pending.remove(ready)

    return name, ordered


def validate(document: Any) -> dict[str, Any]:
    name, ordered = _parse(document)
    return {
        "schema_version": SCHEMA_VERSION,
        "name": name,
        "node_count": len(ordered),
        "execution_order": [node.id for node in ordered],
        "remote_requests": False,
    }


def _input_paths(paths: Any) -> list[Path]:
    if not isinstance(paths, list) or not 1 <= len(paths) <= MAX_INPUTS:
        raise _error("invalid_inputs", "Provide between 1 and 128 input files.")
    result: list[Path] = []
    seen: set[str] = set()
    for path in paths:
        if not isinstance(path, str) or not path or "\x00" in path:
            raise _error("invalid_inputs", "Inputs must be nonempty file paths.")
        candidate = Path(path).expanduser().resolve()
        if not candidate.is_file():
            raise _error("invalid_inputs", "One of the selected input files does not exist.")
        key = os.path.normcase(str(candidate))
        if key in seen:
            raise _error("duplicate_input", "Duplicate input paths are not supported.")
        seen.add(key)
        result.append(candidate)
    return result


def _sourced(sources: tuple[str, ...], available: dict[str, list[Any]]) -> list[Any]:
    result: list[Any] = []
    for source in sources:
        result.extend(available[source])
    return result


def _check_compatibility(spec: ActionSpec, inputs: list[Artifact], node_id: str) -> None:
    if not inputs:
        raise _error("empty_node_input", f"Node {node_id} has no file outputs to process.", node_id)
    if len(inputs) > MAX_INPUTS:
        raise _error("input_limit", f"Node {node_id} exceeds the 128-file limit.", node_id)
    if spec.scope == "batch" and len(inputs) < 2:
        raise _error("batch_size", f"Node {node_id} needs at least two files.", node_id)
    if not all(item.is_file for item in inputs):
        raise _error("directory_input", f"Node {node_id} cannot consume a directory output.", node_id)
    if spec.suffixes is not None:
        for item in inputs:
            if item.suffix not in spec.suffixes:
                raise _error("incompatible_input", f"Node {node_id} does not support input type {item.suffix or '(none)'}.", node_id)


def _project(spec: ActionSpec, inputs: list[Artifact]) -> list[Artifact]:
    count = 1 if spec.scope == "batch" else len(inputs)
    if spec.output == "none":
        return []
    if spec.output == "directory":
        return [Artifact("", False) for _ in range(count)]
    if spec.output == "same":
        return [Artifact(item.suffix, True) for item in inputs]
    return [Artifact(spec.extension, True) for _ in range(count)]


def plan(document: Any, paths: Any, capabilities: list[str] | None = None) -> dict[str, Any]:
    name, nodes = _parse(document)
    chosen = _input_paths(paths)
    virtual: dict[str, list[Artifact]] = {
        "$input": [Artifact(item.suffix.lower()) for item in chosen]
    }
    available_actions = set(capabilities) if capabilities is not None else None
    steps: list[dict[str, Any]] = []
    count = 0
    for node in nodes:
        spec = ACTIONS[node.action_id]
        inputs = _sourced(node.sources, virtual)
        _check_compatibility(spec, inputs, node.id)
        count += len(inputs) if spec.scope == "file" else 1
        if count > MAX_INVOCATIONS:
            raise _error("invocation_limit", "Workflow would exceed the 1,024-operation limit.", node.id)
        virtual[node.id] = _project(spec, inputs)
        steps.append({
            "id": node.id,
            "action_id": node.action_id,
            "sources": list(node.sources),
            "scope": spec.scope,
            "input_count": len(inputs),
            "output_count": len(virtual[node.id]),
            "available": node.action_id in available_actions if available_actions is not None else None,
        })

    return {
        "schema_version": SCHEMA_VERSION,
        "name": name,
        "input_count": len(chosen),
        "operation_count": count,
        "execution_order": [node.id for node in nodes],
        "steps": steps,
        "remote_requests": False,
    }


def run(
    document: Any,
    paths: Any,
    capabilities: list[str],
    dispatch: Callable[[dict[str, Any]], dict[str, Any]],
    *,
    on_event: Callable[[dict[str, Any]], None] | None = None,
    should_stop: Callable[[], bool] | None = None,
) -> dict[str, Any]:
    """Run a validated local workflow, optionally reporting fine-grained progress.

    The cancellation callback is only polled *between* local action invocations.
    Never interrupt a PDF write or voice synthesis in the middle of its output.
    Existing 4-positional-argument callers keep the original contract.
    """
    preflight = plan(document, paths, capabilities)
    for step in preflight["steps"]:
        if not step["available"]:
            raise _error("unavailable_action", f"Action {step['action_id']} is not installed.", step["id"])

    _, nodes = _parse(document)
    original = [str(p) for p in _input_paths(paths)]
    resources: dict[str, list[str]] = {"$input": original}
    records: list[dict[str, Any]] = []
    created: list[str] = []
    run_id = uuid.uuid4().hex
    completed = 0
    total = preflight["operation_count"]

    def emit(event_type: str, **fields: Any) -> None:
        if on_event is not None:
            on_event({
                "schema_version": SCHEMA_VERSION,
                "event": event_type,
                "run_id": run_id,
                "completed_operations": completed,
                "total_operations": total,
                **fields,
            })

    def terminal(
        code: str, message: str, node_id: str, status: str,
    ) -> dict[str, Any]:
        result: dict[str, Any] = {
            "ok": False,
            "schema_version": SCHEMA_VERSION,
            "run_id": run_id,
            "status": status,
            "failed_node": node_id if status == "failed" else "",
            "error": {"code": code, "message": message},
            "steps": records,
            "created_output_paths": created,
            "completed_operations": completed,
            "total_operations": total,
            "remote_requests": False,
        }
        emit("workflow.stopped" if status == "stopped" else "workflow.failed",
             node_id=node_id, status=status, error_code=code)
        return result

    emit("workflow.started", name=preflight["name"], status="running")
    for node in nodes:
        spec = ACTIONS[node.action_id]
        incoming = _sourced(node.sources, resources)
        invocations = [
            {"command": "run_batch", "action_id": node.action_id, "paths": incoming}
        ] if spec.scope == "batch" else [
            {"command": "run", "action_id": node.action_id, "path": item} for item in incoming
        ]
        outputs: list[str] = []
        record: dict[str, Any] = {
            "id": node.id,
            "action_id": node.action_id,
            "status": "running",
            "input_count": len(incoming),
            "completed_count": 0,
            "outputs": outputs,
        }
        records.append(record)
        emit("workflow.node_started", node_id=node.id, action_id=node.action_id,
             status="running", node_total=len(invocations))

        for request in invocations:
            if should_stop is not None and should_stop():
                record["status"] = "stopped"
                return terminal(
                    "stopped", "Stopped safely before the next file action.",
                    node.id, "stopped",
                )

            emit("workflow.action_started", node_id=node.id, action_id=node.action_id,
                 status="running", node_completed=record["completed_count"],
                 node_total=len(invocations))
            try:
                result = dispatch(request)
            except Exception:
                result = {
                    "ok": False,
                    "error": {
                        "code": "worker_failure",
                        "message": "Local worker raised an exception.",
                    },
                }

            if not isinstance(result, dict) or result.get("ok") is not True:
                issue = result.get("error", {}) if isinstance(result, dict) else {}
                code = issue.get("code", "worker_failure") if isinstance(issue, dict) else "worker_failure"
                message = issue.get("message", "Local action failed.") if isinstance(issue, dict) else "Local action failed."
                record["status"] = "failed"
                return terminal(str(code), str(message), node.id, "failed")

            if spec.output != "none":
                value = result.get("output_path")
                if not isinstance(value, str) or not value:
                    record["status"] = "failed"
                    return terminal(
                        "missing_output", "Action reported success without an output path.",
                        node.id, "failed",
                    )
                target = Path(value).resolve()
                correct_kind = target.is_file() if spec.output != "directory" else target.is_dir()
                if not correct_kind or target in (Path(path).resolve() for path in incoming):
                    record["status"] = "failed"
                    return terminal(
                        "invalid_output", "Action output is missing or overwrites an input.",
                        node.id, "failed",
                    )
                outputs.append(str(target))
                created.append(str(target))

            record["completed_count"] += 1
            completed += 1
            emit("workflow.action_completed", node_id=node.id, action_id=node.action_id,
                 status="running", node_completed=record["completed_count"],
                 node_total=len(invocations))

        record["status"] = "completed"
        resources[node.id] = outputs
        emit("workflow.node_completed", node_id=node.id, action_id=node.action_id,
             status="completed", node_total=len(invocations),
             node_completed=len(invocations))

    used = {source for node in nodes for source in node.sources if source != "$input"}
    leaf_paths = [path for node in nodes if node.id not in used for path in resources[node.id]]
    result = {
        "ok": True,
        "schema_version": SCHEMA_VERSION,
        "run_id": run_id,
        "name": preflight["name"],
        "status": "completed",
        "steps": records,
        "output_paths": leaf_paths,
        "created_output_paths": created,
        "completed_operations": completed,
        "total_operations": total,
        "remote_requests": False,
    }
    emit("workflow.completed", status="completed", output_count=len(leaf_paths))
    return result
