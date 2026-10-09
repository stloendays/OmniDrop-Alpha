"""End-to-end duplicate finder contract tests for CLI/GUI shared batch action."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

import duplicate_finder
import worker
import workflow_engine


class DuplicateFinderTests(unittest.TestCase):
    def test_exact_content_groups_not_filenames_or_sizes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = [root / name for name in (
                "a.txt", "same-content.pdf", "different.txt", "sizeA.bin", "sizeB.bin"
            )]
            for path, data in zip(paths, (b"same", b"same", b"diff", b"xx", b"xy")):
                path.write_bytes(data)
            originals = [p.read_bytes() for p in paths]

            request = {
                "command": "run_batch",
                "action_id": "file.find_duplicates",
                "paths": [str(p) for p in paths],
            }
            response = worker.handle(request)
            self.assertTrue(response["ok"], response)
            self.assertEqual(response["duplicate_group_count"], 1)
            self.assertEqual(response["duplicate_file_count"], 1)
            self.assertEqual(response["potential_reclaimable_bytes"], 4)

            report = json.loads(Path(response["output_path"]).read_text("utf-8"))
            self.assertEqual(report["input_count"], 5)
            self.assertEqual(report["method"], "size+sha256")
            self.assertEqual(len(report["groups"]), 1)
            self.assertEqual(report["groups"][0]["size_bytes"], 4)
            self.assertEqual(
                report["groups"][0]["files"],
                [str(paths[0].resolve()), str(paths[1].resolve())],
            )
            self.assertEqual([p.read_bytes() for p in paths], originals)
            self.assertIn("file.find_duplicates", worker.capabilities())
            again = worker.handle(request)
            self.assertTrue(again["ok"], again)
            self.assertNotEqual(response["output_path"], again["output_path"])

    def test_equal_size_but_different_bytes_are_not_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = Path(directory) / "a.dat", Path(directory) / "b.dat"
            a.write_bytes(b"x")
            b.write_bytes(b"y")
            response = worker.handle({
                "command": "run_batch",
                "action_id": "file.find_duplicates",
                "paths": [str(a), str(b)],
            })
            self.assertTrue(response["ok"], response)
            self.assertEqual(response["duplicate_group_count"], 0)
            self.assertEqual(response["potential_reclaimable_bytes"], 0)

    def test_repeated_path_and_bounds_rejected_without_new_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a = root / "a.txt"
            a.write_bytes(b"a")
            bad = worker.handle({
                "command": "run_batch",
                "action_id": "file.find_duplicates",
                "paths": [str(a), str(a)],
            })
            self.assertFalse(bad["ok"])
            self.assertEqual(bad["error"]["code"], "invalid_request")
            self.assertEqual(list(root.iterdir()), [a])

            b = root / "b.txt"
            b.write_bytes(b"b")
            with patch.object(duplicate_finder, "MAX_TOTAL_BYTES", 1):
                too_big = worker.handle({
                    "command": "run_batch",
                    "action_id": "file.find_duplicates",
                    "paths": [str(a), str(b)],
                })
            self.assertFalse(too_big["ok"])
            self.assertEqual(too_big["error"]["code"], "invalid_request")
            self.assertEqual(sorted(root.iterdir()), sorted([a, b]))

    def test_validated_workflow_batch_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a, b = root / "a.txt", root / "b.jpg"
            a.write_bytes(b"same")
            b.write_bytes(b"same")
            workflow = {
                "schema_version": 1,
                "name": "Duplicate report",
                "nodes": [
                    {
                        "id": "report",
                        "action_id": "file.find_duplicates",
                        "sources": ["$input"],
                    }
                ],
            }
            planned = workflow_engine.plan(workflow, [str(a), str(b)], worker.capabilities())
            self.assertEqual(planned["operation_count"], 1)
            self.assertEqual(planned["steps"][0]["output_count"], 1)
            result = worker.handle({
                "command": "workflow.run",
                "workflow": workflow,
                "paths": [str(a), str(b)],
            })
            self.assertTrue(result["ok"], result)
            report = json.loads(Path(result["output_paths"][0]).read_text("utf-8"))
            self.assertEqual(report["duplicate_group_count"], 1)


if __name__ == "__main__":
    unittest.main()
