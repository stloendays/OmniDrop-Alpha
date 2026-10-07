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
        if worker.pillow_available():
            self.assertIn("image.rotate_clockwise", result["actions"])
            self.assertIn("image.rotate_counterclockwise", result["actions"])
        if worker.pypdf_available():
            self.assertIn("pdf.merge", result["actions"])

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

    @unittest.skipUnless(worker.pillow_available(), "Pillow is not installed")
    def test_image_rotation_preserves_source_and_direction(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "grid.png"

            image = Image.new("RGB", (2, 3))
            image.putdata(
                [
                    (255, 0, 0), (0, 255, 0),
                    (0, 0, 255), (255, 255, 0),
                    (255, 0, 255), (0, 255, 255),
                ]
            )
            image.save(path)
            before = path.read_bytes()

            clockwise = worker.handle(
                {"command": "run", "action_id": "image.rotate_clockwise", "path": str(path)}
            )
            self.assertTrue(clockwise["ok"])
            self.assertEqual(path.read_bytes(), before)

            with Image.open(clockwise["output_path"]) as rotated:
                self.assertEqual(rotated.size, (3, 2))
                self.assertEqual(
                    list(rotated.getdata()),
                    [
                        (255, 0, 255), (0, 0, 255), (255, 0, 0),
                        (0, 255, 255), (255, 255, 0), (0, 255, 0),
                    ],
                )

            counterclockwise = worker.handle(
                {"command": "run", "action_id": "image.rotate_counterclockwise", "path": str(path)}
            )
            self.assertTrue(counterclockwise["ok"])
            self.assertEqual(path.read_bytes(), before)

            with Image.open(counterclockwise["output_path"]) as rotated:
                self.assertEqual(rotated.size, (3, 2))
                self.assertEqual(
                    list(rotated.getdata()),
                    [
                        (0, 255, 0), (255, 255, 0), (0, 255, 255),
                        (255, 0, 0), (0, 0, 255), (255, 0, 255),
                    ],
                )

    @unittest.skipUnless(worker.pillow_available(), "Pillow is not installed")
    def test_animated_image_rotation_is_rejected(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "animated.gif"
            first = Image.new("RGB", (2, 2), "red")
            second = Image.new("RGB", (2, 2), "blue")
            first.save(path, save_all=True, append_images=[second], duration=50, loop=0)

            result = worker.handle(
                {"command": "run", "action_id": "image.rotate_clockwise", "path": str(path)}
            )
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "image_error")

    @unittest.skipUnless(worker.pypdf_available(), "pypdf is not installed")
    def test_pdf_merge_preserves_inputs_and_order(self):
        from pypdf import PdfReader, PdfWriter

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.pdf"
            second = root / "second.pdf"

            writer = PdfWriter()
            writer.add_blank_page(width=100, height=100)
            writer.add_blank_page(width=110, height=110)
            with first.open("wb") as handle:
                writer.write(handle)

            writer = PdfWriter()
            writer.add_blank_page(width=200, height=200)
            with second.open("wb") as handle:
                writer.write(handle)

            first_before = first.read_bytes()
            second_before = second.read_bytes()

            result = worker.handle(
                {
                    "command": "run_batch",
                    "action_id": "pdf.merge",
                    "paths": [str(first), str(second)],
                }
            )

            self.assertTrue(result["ok"])
            self.assertEqual(result["input_count"], 2)
            self.assertEqual(result["page_count"], 3)
            self.assertEqual(first.read_bytes(), first_before)
            self.assertEqual(second.read_bytes(), second_before)

            merged = PdfReader(result["output_path"])
            self.assertEqual(len(merged.pages), 3)
            self.assertEqual(float(merged.pages[0].mediabox.width), 100.0)
            self.assertEqual(float(merged.pages[1].mediabox.width), 110.0)
            self.assertEqual(float(merged.pages[2].mediabox.width), 200.0)

    @unittest.skipUnless(worker.pypdf_available(), "pypdf is not installed")
    def test_pdf_merge_requires_two_inputs(self):
        from pypdf import PdfWriter

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "one.pdf"
            writer = PdfWriter()
            writer.add_blank_page(width=100, height=100)
            with path.open("wb") as handle:
                writer.write(handle)

            result = worker.handle(
                {
                    "command": "run_batch",
                    "action_id": "pdf.merge",
                    "paths": [str(path)],
                }
            )
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "invalid_request")

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
