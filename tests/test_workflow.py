"""Contract, security, graph, and integration tests for OmniDrop Workflow v1."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

import workflow_engine as wf
import worker


def definition(nodes, name="My Workflow"):
    return {"schema_version": 1, "name": name, "nodes": nodes}


def node(node_id, action_id, sources=None):
    return {"id": node_id, "action_id": action_id, "sources": sources or ["$input"]}


class WorkflowTests(unittest.TestCase):
    def assertCode(self, fn, code):
        with self.assertRaises(wf.WorkflowError) as failure:
            fn()
        self.assertEqual(failure.exception.code, code)

    def test_topological_order_branching_and_join(self):
        document = definition([
            node("last", "text.deduplicate", ["first"]),
            node("branch", "text.deduplicate"),
            node("first", "text.normalize"),
        ])
        validation = wf.validate(document)
        self.assertEqual(validation["execution_order"], ["branch", "first", "last"])
        self.assertFalse(validation["remote_requests"])

        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "data.txt"
            source.write_text("a  \nb  \na  \n", encoding="utf-8")
            plan = wf.plan(document, [str(source)], worker.capabilities())
            self.assertEqual(plan["operation_count"], 3)
            before = sorted(Path(temp).iterdir())
            self.assertEqual(len(before), 1)
            result = wf.run(document, [str(source)], worker.capabilities(), worker.handle)
            self.assertTrue(result["ok"], result)
            self.assertEqual(source.read_text(), "a  \nb  \na  \n")
            self.assertEqual(len(result["output_paths"]), 2)
            self.assertEqual(len(result["created_output_paths"]), 3)
            self.assertEqual(
                [row["id"] for row in result["steps"]],
                ["branch", "first", "last"],
            )
            self.assertTrue(all(Path(p).is_file() for p in result["output_paths"]))
            self.assertEqual(len(before), 1)  # preflight did not write files

    def test_pdf_merge_then_extract_text(self):
        if not worker.pypdf_available():
            self.skipTest("pypdf not installed")
        from pypdf import PdfWriter

        document = definition([
            node("extract", "pdf.extract_text", ["merged"]),
            node("merged", "pdf.merge", ["$input"]),
        ])
        with tempfile.TemporaryDirectory() as temp:
            paths = []
            for i in range(2):
                path = Path(temp) / f"page{i}.pdf"
                writer = PdfWriter()
                writer.add_blank_page(width=200, height=200)
                with path.open("wb") as stream:
                    writer.write(stream)
                paths.append(str(path))
            plan = wf.plan(document, paths, worker.capabilities())
            self.assertEqual(plan["execution_order"], ["merged", "extract"])
            self.assertEqual(plan["steps"][0]["input_count"], 2)
            self.assertEqual(plan["steps"][1]["input_count"], 1)
            result = wf.run(document, paths, worker.capabilities(), worker.handle)
            self.assertTrue(result["ok"], result)
            self.assertEqual(len(result["output_paths"]), 1)
            self.assertEqual(Path(result["output_paths"][0]).suffix, ".txt")
            self.assertEqual(len(result["created_output_paths"]), 2)

    def test_branches_can_join_outputs_into_one_pdf(self):
        if not worker.pypdf_available():
            self.skipTest("pypdf not installed")
        from pypdf import PdfReader, PdfWriter

        document = definition([
            node("join", "pdf.merge", ["cw", "ccw"]),
            node("ccw", "pdf.rotate_counterclockwise"),
            node("cw", "pdf.rotate_clockwise"),
        ])
        with tempfile.TemporaryDirectory() as temp:
            inputs = []
            for index in range(2):
                path = Path(temp) / f"source-{index}.pdf"
                writer = PdfWriter()
                writer.add_blank_page(width=210, height=180)
                with path.open("wb") as stream:
                    writer.write(stream)
                inputs.append(str(path))

            plan = wf.plan(document, inputs, worker.capabilities())
            self.assertEqual(plan["execution_order"], ["ccw", "cw", "join"])
            self.assertEqual(plan["steps"][-1]["input_count"], 4)
            output = wf.run(document, inputs, worker.capabilities(), worker.handle)
            self.assertTrue(output["ok"], output)
            self.assertEqual(len(output["output_paths"]), 1)
            self.assertEqual(len(PdfReader(output["output_paths"][0]).pages), 4)

    def test_validate_rejects_cycles_duplicates_and_unknown_references(self):
        self.assertCode(lambda: wf.validate(definition([
            node("a", "text.normalize", ["b"]), node("b", "text.normalize", ["a"])
        ])), "dependency_cycle")
        self.assertCode(lambda: wf.validate(definition([
            node("a", "text.normalize"), node("a", "text.normalize")
        ])), "duplicate_node")
        self.assertCode(lambda: wf.validate(definition([
            node("a", "text.normalize", ["missing"])
        ])), "unknown_source")
        self.assertCode(lambda: wf.validate(definition([
            node("bad Name", "text.normalize")
        ])), "invalid_node")
        self.assertCode(lambda: wf.validate(definition([
            {"id": "a", "action_id": "text.normalize", "sources": ["$input"], "shell": "echo hacked"}
        ])), "invalid_node")

    def test_remote_translation_shell_actions_are_not_allowed(self):
        for action in ("translate_file", "workflow.run", "shell.exec", "python.exec", "text.translate"):
            self.assertCode(
                lambda action=action: wf.validate(definition([node("step", action)])),
                "unsupported_action",
            )
        self.assertCode(lambda: wf.validate({
            **definition([node("a", "text.normalize")]),
            "allow_remote": True,
        }), "invalid_workflow")

    def test_validation_is_readonly_and_preflight_catches_type_mismatch(self):
        with tempfile.TemporaryDirectory() as temp:
            image = Path(temp) / "image.png"
            image.write_bytes(b"dummy")
            document = definition([
                node("image", "image.convert_webp"),
                node("bad", "pdf.extract_text", ["image"]),
            ])
            self.assertCode(lambda: wf.plan(document, [str(image)]), "incompatible_input")
            self.assertEqual(list(Path(temp).iterdir()), [image])

    def test_directory_or_hash_cannot_feed_downstream_file_action(self):
        with tempfile.TemporaryDirectory() as temp:
            file = Path(temp) / "safe.zip"
            file.write_bytes(b"placeholder")
            bad_directory = definition([
                node("extract", "archive.extract"),
                node("hash", "file.sha256", ["extract"]),
            ])
            self.assertCode(lambda: wf.plan(bad_directory, [str(file)]), "directory_input")
            text = Path(temp) / "safe.txt"
            text.write_text("A")
            bad_hash = definition([
                node("hash", "file.sha256"),
                node("normalize", "text.normalize", ["hash"]),
            ])
            self.assertCode(lambda: wf.plan(bad_hash, [str(text)]), "empty_node_input")

    def test_capabilities_are_checked_before_first_mutation(self):
        document = definition([node("normalize", "text.normalize")])
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "a.txt"
            source.write_text("a  \n")
            planned = wf.plan(document, [str(source)], [])
            self.assertFalse(planned["steps"][0]["available"])
            self.assertCode(
                lambda: wf.run(document, [str(source)], [], worker.handle),
                "unavailable_action",
            )
            self.assertEqual(list(Path(temp).iterdir()), [source])

    def test_failure_returns_partial_outputs_with_run_id(self):
        document = definition([
            node("normalize", "text.normalize"),
            node("dedup", "text.deduplicate", ["normalize"]),
        ])
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "a.txt"
            path.write_text("one  \n")
            calls = 0

            def dispatch(request):
                nonlocal calls
                calls += 1
                if calls == 2:
                    return {"ok": False, "error": {"code": "simulated", "message": "Simulated failure"}}
                return worker.handle(request)

            result = wf.run(document, [str(path)], worker.capabilities(), dispatch)
            self.assertFalse(result["ok"])
            self.assertEqual(result["failed_node"], "dedup")
            self.assertEqual(result["error"]["code"], "simulated")
            self.assertEqual(len(result["created_output_paths"]), 1)
            self.assertTrue(Path(result["created_output_paths"][0]).exists())
            self.assertEqual(result["steps"][0]["status"], "completed")
            self.assertEqual(result["steps"][1]["status"], "failed")
            self.assertEqual(len(result["run_id"]), 32)

    def test_input_bounds_and_no_embedded_file_paths(self):
        document = definition([node("step", "text.normalize")])
        self.assertCode(lambda: wf.plan(document, []), "invalid_inputs")
        self.assertCode(lambda: wf.plan(document, ["missing.txt"]), "invalid_inputs")
        self.assertCode(lambda: wf.validate(definition([node(
            "step", "text.normalize", ["$input", "$input"]
        )])), "invalid_source")
        self.assertCode(lambda: wf.validate({
            **definition([node("step", "text.normalize")]), "paths": ["/tmp/secret"]
        }), "invalid_workflow")

    def test_worker_protocol_validate_plan_execute(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "sample.txt"
            path.write_text("A  \nA  \n", encoding="utf-8")
            document = definition([
                node("normalize", "text.normalize"),
                node("unique", "text.deduplicate", ["normalize"]),
            ])
            validate_result = worker.handle({"command": "workflow.validate", "workflow": document})
            self.assertTrue(validate_result["ok"], validate_result)
            plan_result = worker.handle({
                "command": "workflow.plan", "workflow": document, "paths": [str(path)]
            })
            self.assertTrue(plan_result["ok"], plan_result)
            self.assertEqual(len(list(Path(temp).iterdir())), 1)
            output = worker.handle({
                "command": "workflow.run", "workflow": document, "paths": [str(path)]
            })
            self.assertTrue(output["ok"], output)
            self.assertEqual(Path(output["output_paths"][0]).read_text(), "A\n")
            self.assertEqual(path.read_text(), "A  \nA  \n")

    def test_invalid_worker_command_is_rejected(self):
        result = worker.handle({
            "command": "workflow.run",
            "workflow": definition([node("bad", "python.exec")]),
            "paths": [],
        })
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "unsupported_action")


if __name__ == "__main__":
    unittest.main()
