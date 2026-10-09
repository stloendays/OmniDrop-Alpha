"""Regression tests for lossless, bounded PDF embedded-image extraction."""

from __future__ import annotations

import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

import pdf_image_extraction
import worker
import workflow_engine


def image_pdf(path: Path, pages: int) -> bytes:
    """Build a PDF containing actual JPEG XObjects, without external fixtures."""
    from PIL import Image
    from pypdf import PdfWriter
    from pypdf.generic import DictionaryObject, NameObject, NumberObject, StreamObject

    image = Image.new("RGB", (12, 8), (55, 11, 99))
    stream = io.BytesIO()
    image.save(stream, format="JPEG")
    jpeg = stream.getvalue()

    writer = PdfWriter()
    for _ in range(pages):
        page = writer.add_blank_page(width=200, height=160)
        image_object = StreamObject()
        image_object._data = jpeg
        for key, value in {
            "/Type": NameObject("/XObject"),
            "/Subtype": NameObject("/Image"),
            "/Width": NumberObject(12),
            "/Height": NumberObject(8),
            "/ColorSpace": NameObject("/DeviceRGB"),
            "/BitsPerComponent": NumberObject(8),
            "/Filter": NameObject("/DCTDecode"),
        }.items():
            image_object[NameObject(key)] = value
        image_ref = writer._add_object(image_object)
        page[NameObject("/Resources")] = DictionaryObject({
            NameObject("/XObject"): DictionaryObject({NameObject("/Image1"): image_ref}),
        })

    with path.open("wb") as output:
        writer.write(output)
    return jpeg


@unittest.skipUnless(
    worker.pypdf_available() and worker.pillow_available(),
    "PDF image extraction needs pypdf and Pillow",
)
class PdfImageExtractionTests(unittest.TestCase):
    def test_worker_action_extracts_images_without_changing_pdf(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "report.pdf"
            original_jpeg = image_pdf(source, 2)
            original_pdf = source.read_bytes()
            request = {"command": "run", "action_id": "pdf.extract_images", "path": str(source)}

            self.assertIn("pdf.extract_images", worker.capabilities())
            first = worker.handle(request)
            self.assertTrue(first["ok"], first)
            self.assertEqual(first["image_count"], 2)
            result_dir = Path(first["output_path"])
            self.assertTrue(result_dir.is_dir())
            self.assertEqual(
                sorted(item.name for item in result_dir.iterdir()),
                ["page-0001-image-001.jpg", "page-0002-image-001.jpg"],
            )
            self.assertEqual((result_dir / "page-0001-image-001.jpg").read_bytes(), original_jpeg)
            self.assertEqual(source.read_bytes(), original_pdf)

            second = worker.handle(request)
            self.assertTrue(second["ok"], second)
            self.assertNotEqual(first["output_path"], second["output_path"])
            self.assertEqual(source.read_bytes(), original_pdf)

    def test_empty_pages_create_an_empty_output_folder(self):
        from pypdf import PdfWriter

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "blank.pdf"
            writer = PdfWriter()
            writer.add_blank_page(width=100, height=100)
            with source.open("wb") as output:
                writer.write(output)
            result = worker.handle(
                {"command": "run", "action_id": "pdf.extract_images", "path": str(source)}
            )
            self.assertTrue(result["ok"], result)
            self.assertEqual(result["image_count"], 0)
            self.assertEqual(list(Path(result["output_path"]).iterdir()), [])

    def test_limits_rollback_partial_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "many.pdf"
            image_pdf(source, 2)
            original = source.read_bytes()
            with patch.object(pdf_image_extraction, "MAX_IMAGES", 1):
                result = worker.handle(
                    {"command": "run", "action_id": "pdf.extract_images", "path": str(source)}
                )
            self.assertFalse(result["ok"], result)
            self.assertEqual(result["error"]["code"], "pdf_error")
            self.assertEqual(source.read_bytes(), original)
            self.assertFalse((source.parent / "many.images").exists())

    def test_incompatible_input_and_local_dag_semantics(self):
        with tempfile.TemporaryDirectory() as directory:
            txt = Path(directory) / "input.txt"
            txt.write_text("sample", encoding="utf-8")
            bad = worker.handle(
                {"command": "run", "action_id": "pdf.extract_images", "path": str(txt)}
            )
            self.assertFalse(bad["ok"])
            self.assertEqual(bad["error"]["code"], "invalid_request")

            source = Path(directory) / "source.pdf"
            image_pdf(source, 1)
            workflow = {
                "schema_version": 1,
                "name": "Extract PDF images",
                "nodes": [
                    {"id": "extract", "action_id": "pdf.extract_images", "sources": ["$input"]},
                ],
            }
            planned = workflow_engine.plan(workflow, [str(source)], worker.capabilities())
            self.assertTrue(planned["steps"][0]["available"])
            self.assertEqual(planned["steps"][0]["output_count"], 1)
            ran = worker.handle({
                "command": "workflow.run",
                "workflow": workflow,
                "paths": [str(source)],
            })
            self.assertTrue(ran["ok"], ran)
            self.assertTrue(Path(ran["output_paths"][0]).is_dir())
            self.assertEqual(len(ran["created_output_paths"]), 1)

            downstream = {
                "schema_version": 1,
                "name": "Invalid downstream action",
                "nodes": workflow["nodes"] + [
                    {"id": "hash", "action_id": "file.sha256", "sources": ["extract"]},
                ],
            }
            with self.assertRaises(workflow_engine.WorkflowError) as error:
                workflow_engine.plan(downstream, [str(source)])
            self.assertEqual(error.exception.code, "directory_input")


if __name__ == "__main__":
    unittest.main()
