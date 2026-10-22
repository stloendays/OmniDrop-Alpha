import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
import scientific_pdf
import worker

class ScientificPdfTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pdf = Path(self.tmp.name) / "paper.pdf"
        self.pdf.write_bytes(b"%PDF-1.7\nfixture")

    def test_missing_processor_is_unavailable(self):
        with patch.object(scientific_pdf, "find_cli", return_value=None):
            self.assertNotIn("pdf.parse_scientific", worker.capabilities())
            result = worker.handle({"command": "run", "action_id": "pdf.parse_scientific", "path": str(self.pdf)})
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "missing_dependency")

    def test_three_formats_and_no_overwrite(self):
        before = self.pdf.read_bytes()
        def pretend(command, **kwargs):
            output = Path(command[4])
            self.assertEqual(command[1], "parse")
            self.assertEqual(command[5:7], ["--tier", "standard"])
            self.assertEqual(command[7], "--format")
            self.assertNotIn("MINERU_API_URL", kwargs["env"])
            output.parent.mkdir(parents=True, exist_ok=True)
            if command[-1] == "middle_json":
                output.write_text('{"schema":"docvortex.middle","schema_version":"2.0"}', encoding="utf-8")
            elif command[-1] == "zip":
                with zipfile.ZipFile(output, "w") as z:
                    z.writestr("markdown.md", "# test")
            else:
                output.write_text("# test\n", encoding="utf-8")
                (output.parent / "image.png").write_bytes(b"image")
            return SimpleNamespace(returncode=0, stdout="", stderr="")
        with patch.object(scientific_pdf, "find_cli", return_value="mineru-kit"), patch.object(scientific_pdf.subprocess, "run", side_effect=pretend), patch.dict(os.environ, {"MINERU_API_URL":"https://example.org"}):
            for action in scientific_pdf.FORMATS:
                result = worker.handle({"command": "run", "action_id": action, "path": str(self.pdf)})
                self.assertTrue(result["ok"], result)
                self.assertTrue(Path(result["output_path"]).is_file())
                self.assertEqual(self.pdf.read_bytes(), before)
            again = worker.handle({"command": "run", "action_id": "pdf.parse_scientific", "path": str(self.pdf)})
            self.assertTrue(again["ok"])
            self.assertIn("-2/", again["output_path"])

    def test_failure_removes_partial_files(self):
        def fail(command, **kwargs):
            Path(command[4]).write_text("partial", encoding="utf-8")
            return SimpleNamespace(returncode=2, stdout="", stderr="test failure")
        with patch.object(scientific_pdf, "find_cli", return_value="mineru-kit"), patch.object(scientific_pdf.subprocess, "run", side_effect=fail):
            result = worker.handle({"command": "run", "action_id": "pdf.parse_scientific", "path": str(self.pdf)})
            self.assertEqual(result["error"]["code"], "mineru_error")
            self.assertEqual(list(Path(self.tmp.name).glob(".omnidrop-mineru-*")), [])

    def test_rejects_invalid_middle_json(self):
        def invalid(command, **kwargs):
            Path(command[4]).write_text('{"pdf_info":[]}', encoding="utf-8")
            return SimpleNamespace(returncode=0, stdout="", stderr="")
        with patch.object(scientific_pdf, "find_cli", return_value="mineru-kit"), patch.object(scientific_pdf.subprocess, "run", side_effect=invalid):
            result = worker.handle({"command": "run", "action_id": "pdf.parse_scientific_json", "path": str(self.pdf)})
            self.assertEqual(result["error"]["code"], "invalid_output")

if __name__ == "__main__":
    unittest.main()
