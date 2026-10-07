import hashlib
import tempfile
import unittest
import zipfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
import worker  # noqa: E402


class WorkerTests(unittest.TestCase):
    def test_ping(self):
        self.assertTrue(worker.handle({"command": "ping"})["ok"])

    def test_capabilities_have_core_actions(self):
        result = worker.handle({"command": "capabilities"})
        self.assertTrue(result["ok"])
        self.assertIn("file.sha256", result["actions"])
        self.assertIn("text.normalize", result["actions"])
        self.assertIn("text.format_json", result["actions"])
        self.assertIn("text.format_xml", result["actions"])
        self.assertIn("archive.extract", result["actions"])

    def test_sha256(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.txt"
            path.write_bytes(b"omnidrop")
            result = worker.handle({"command": "run", "action_id": "file.sha256", "path": str(path)})
            self.assertTrue(result["ok"])
            self.assertEqual(result["sha256"], hashlib.sha256(b"omnidrop").hexdigest())

    def test_text_normalize_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.txt"
            path.write_bytes(b"a  \r\nb\t \r\n")
            before = path.read_bytes()
            result = worker.handle({"command": "run", "action_id": "text.normalize", "path": str(path)})
            self.assertTrue(result["ok"])
            self.assertEqual(path.read_bytes(), before)
            output = Path(result["output_path"])
            self.assertNotEqual(output, path)
            self.assertEqual(output.read_text(encoding="utf-8"), "a\nb\n")

    def test_deduplicate(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.txt"
            path.write_text("a\nb\na\n", encoding="utf-8")
            result = worker.handle({"command": "run", "action_id": "text.deduplicate", "path": str(path)})
            self.assertTrue(result["ok"])
            self.assertEqual(Path(result["output_path"]).read_text(encoding="utf-8"), "a\nb\n")

    def test_json_format_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "data.json"
            path.write_text('{"b":2,"a":[1,true]}', encoding="utf-8")
            before = path.read_text(encoding="utf-8")
            result = worker.handle({"command": "run", "action_id": "text.format_json", "path": str(path)})
            self.assertTrue(result["ok"])
            self.assertEqual(path.read_text(encoding="utf-8"), before)
            output = Path(result["output_path"])
            self.assertNotEqual(output, path)
            self.assertEqual(
                output.read_text(encoding="utf-8"),
                '{\n  "b": 2,\n  "a": [\n    1,\n    true\n  ]\n}\n',
            )

    def test_invalid_json_returns_format_error(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.json"
            path.write_text('{"broken":', encoding="utf-8")
            result = worker.handle({"command": "run", "action_id": "text.format_json", "path": str(path)})
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "format_error")

    def test_xml_format_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "data.xml"
            path.write_text("<root><item id=\"1\">x</item></root>", encoding="utf-8")
            before = path.read_text(encoding="utf-8")
            result = worker.handle({"command": "run", "action_id": "text.format_xml", "path": str(path)})
            self.assertTrue(result["ok"])
            self.assertEqual(path.read_text(encoding="utf-8"), before)
            output = Path(result["output_path"])
            self.assertNotEqual(output, path)
            rendered = output.read_text(encoding="utf-8")
            self.assertIn("<root>", rendered)
            self.assertIn('  <item id="1">x</item>', rendered)

    def test_zip_inspect_and_extract(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "safe.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("folder/a.txt", "hello")
            inspected = worker.handle({"command": "run", "action_id": "archive.inspect", "path": str(path)})
            self.assertTrue(inspected["ok"])
            extracted = worker.handle({"command": "run", "action_id": "archive.extract", "path": str(path)})
            self.assertTrue(extracted["ok"])
            self.assertEqual((Path(extracted["output_path"]) / "folder" / "a.txt").read_text(), "hello")

    def test_zip_slip_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("../escape.txt", "no")
            result = worker.handle({"command": "run", "action_id": "archive.extract", "path": str(path)})
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "archive_error")

    def test_unknown_action(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a.txt"
            path.write_text("x", encoding="utf-8")
            result = worker.handle({"command": "run", "action_id": "unknown.action", "path": str(path)})
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "unsupported_action")


if __name__ == "__main__":
    unittest.main()
