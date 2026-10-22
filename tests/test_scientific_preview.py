import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from scientific_preview import prepare_scientific_preview, PreviewError

VALID = {"schema": "docvortex.middle", "schema_version": "2.0", "is_full_document": True,
         "pages": [{"page_idx": 0, "blocks": [{"type": "text", "content": []}, {"type": "table", "content": []}]},
                   {"page_idx": 1, "blocks": [{"type": "equation", "content": []}]}]}


class PreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.source = self.directory / "paper.pdf"
        self.source.write_bytes(b"%PDF-1.7\ntest")

    def test_markdown_creates_source_provenance_and_text_preview(self):
        output = self.directory / "paper.md"
        output.write_text("# Title\n\\(E=mc^2\\)\n", encoding="utf-8")
        before = self.source.read_bytes()
        info = prepare_scientific_preview(self.source, output, "markdown", self.directory)
        self.assertEqual(before, self.source.read_bytes())
        self.assertIn("E=mc^2", Path(info["preview_path"]).read_text(encoding="utf-8"))
        data = json.loads(Path(info["provenance_path"]).read_text(encoding="utf-8"))
        self.assertEqual(data["validation_status"], "not_manually_verified")
        self.assertEqual(data["source_bytes"], len(before))
        self.assertEqual(len(data["source_sha256"]), 64)

    def test_middle_json_page_counts(self):
        output = self.directory / "paper.json"
        output.write_text(json.dumps(VALID), encoding="utf-8")
        info = prepare_scientific_preview(self.source, output, "middle_json", self.directory)
        self.assertEqual(info["page_count"], 2)
        data = json.loads(Path(info["provenance_path"]).read_text())
        self.assertEqual(data["block_types"], {"equation": 1, "table": 1, "text": 1})
        self.assertEqual(data["page_indices"], [0, 1])

    def test_invalid_protocol_is_rejected(self):
        output = self.directory / "paper.json"
        output.write_text(json.dumps({"schema": "docvortex.middle", "schema_version": "1", "pages": []}))
        with self.assertRaises(PreviewError):
            prepare_scientific_preview(self.source, output, "middle_json", self.directory)

    def test_zip_figure_names_are_never_used_for_write(self):
        output = self.directory / "paper.zip"
        with zipfile.ZipFile(output, "w") as z:
            z.writestr("middle_json.json", json.dumps(VALID))
            z.writestr("markdown.md", "# Header\nTable")
            z.writestr("images/../../malicious.png", b"fake-image")
        info = prepare_scientific_preview(self.source, output, "zip", self.directory)
        self.assertEqual(info["page_count"], 2)
        self.assertEqual(len(info["preview_images"]), 1)
        self.assertTrue(Path(info["preview_images"][0]).name.startswith("figure_"))
        self.assertFalse((self.directory / "malicious.png").exists())

    def test_zip_requires_both_core_outputs(self):
        output = self.directory / "paper.zip"
        with zipfile.ZipFile(output, "w") as z:
            z.writestr("markdown.md", "# Header")
        with self.assertRaises(PreviewError):
            prepare_scientific_preview(self.source, output, "zip", self.directory)


if __name__ == "__main__":
    unittest.main()
