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
            self.assertIn("image.resize_half", result["actions"])
        if worker.pillow_available():
            self.assertIn("image.rotate_clockwise", result["actions"])
            self.assertIn("image.rotate_counterclockwise", result["actions"])
        if worker.pypdf_available():
            self.assertIn("pdf.rotate_clockwise", result["actions"])
            self.assertIn("pdf.rotate_counterclockwise", result["actions"])
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

    @unittest.skipUnless(worker.pillow_available(), "Pillow is not installed")
    def test_resize_half_preserves_source_and_alpha(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "alpha.png"
            Image.new("RGBA", (7, 5), (10, 20, 30, 40)).save(path)
            original_bytes = path.read_bytes()

            request = {"command": "run", "action_id": "image.resize_half", "path": str(path)}
            result = worker.handle(request)
            self.assertTrue(result["ok"], result)
            self.assertEqual(path.read_bytes(), original_bytes)
            self.assertEqual(
                (result["original_width"], result["original_height"]), (7, 5)
            )
            self.assertEqual((result["output_width"], result["output_height"]), (4, 3))
            self.assertNotEqual(Path(result["output_path"]), path)

            with Image.open(result["output_path"]) as resized:
                self.assertEqual(resized.size, (4, 3))
                self.assertEqual(resized.mode, "RGBA")
                self.assertEqual(resized.getpixel((1, 1)), (10, 20, 30, 40))

            second = worker.handle(request)
            self.assertTrue(second["ok"], second)
            self.assertNotEqual(second["output_path"], result["output_path"])
            self.assertEqual(path.read_bytes(), original_bytes)

    @unittest.skipUnless(worker.pillow_available(), "Pillow is not installed")
    def test_resize_half_paletted_png_preserves_transparency(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "indexed.png"
            palette_image = Image.new("P", (8, 8))
            palette_image.putpalette([0, 0, 0, 255, 0, 0] + [0] * 762)
            palette_image.putdata([0 if x < 4 else 1 for _ in range(8) for x in range(8)])
            palette_image.save(path, transparency=0)

            result = worker.handle(
                {"command": "run", "action_id": "image.resize_half", "path": str(path)}
            )
            self.assertTrue(result["ok"], result)
            with Image.open(result["output_path"]) as image:
                self.assertEqual(image.size, (4, 4))
                self.assertEqual(image.mode, "RGBA")
                self.assertEqual(image.getpixel((0, 2))[3], 0)
                self.assertEqual(image.getpixel((3, 2))[3], 255)

    @unittest.skipUnless(worker.pillow_available(), "Pillow is not installed")
    def test_resize_half_normalizes_exif_orientation(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oriented.jpg"
            image = Image.new("RGB", (4, 6), (120, 80, 20))
            exif = Image.Exif()
            exif[274] = 6  # Display 90 degrees clockwise relative to stored pixels.
            exif[315] = "OmniDrop test"
            image.save(path, exif=exif.tobytes())
            original_bytes = path.read_bytes()

            result = worker.handle(
                {"command": "run", "action_id": "image.resize_half", "path": str(path)}
            )
            self.assertTrue(result["ok"], result)
            self.assertEqual(
                (result["original_width"], result["original_height"]), (6, 4)
            )
            self.assertEqual((result["output_width"], result["output_height"]), (3, 2))
            self.assertEqual(path.read_bytes(), original_bytes)

            with Image.open(result["output_path"]) as resized:
                self.assertEqual(resized.size, (3, 2))
                self.assertNotIn(resized.getexif().get(274), (6, 8))
                self.assertEqual(resized.getexif().get(315), "OmniDrop test")

    @unittest.skipUnless(worker.pillow_available(), "Pillow is not installed")
    def test_resize_half_rejects_multiframe_and_unsupported_formats(self):
        from PIL import Image

        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            path = directory / "multipage.tiff"
            first = Image.new("RGB", (8, 8), "red")
            second = Image.new("RGB", (8, 8), "blue")
            first.save(path, save_all=True, append_images=[second])
            before = path.read_bytes()

            result = worker.handle(
                {"command": "run", "action_id": "image.resize_half", "path": str(path)}
            )
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "image_error")
            self.assertIn("Multi-frame", result["error"]["message"])
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(list(directory.glob("multipage.half*")), [])

            svg = directory / "vector.svg"
            svg.write_text("<svg/>", encoding="utf-8")
            result = worker.handle(
                {"command": "run", "action_id": "image.resize_half", "path": str(svg)}
            )
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "image_error")

    @unittest.skipUnless(worker.pypdf_available(), "pypdf is not installed")
    def test_pdf_rotation_preserves_source_and_direction(self):
        from pypdf import PdfReader, PdfWriter

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "source.pdf"

            writer = PdfWriter()
            writer.add_blank_page(width=120, height=200)
            with path.open("wb") as handle:
                writer.write(handle)

            before = path.read_bytes()

            clockwise = worker.handle(
                {"command": "run", "action_id": "pdf.rotate_clockwise", "path": str(path)}
            )
            self.assertTrue(clockwise["ok"])
            self.assertEqual(clockwise["page_count"], 1)
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(PdfReader(clockwise["output_path"]).pages[0].rotation, 90)

            counterclockwise = worker.handle(
                {"command": "run", "action_id": "pdf.rotate_counterclockwise", "path": str(path)}
            )
            self.assertTrue(counterclockwise["ok"])
            self.assertEqual(counterclockwise["page_count"], 1)
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(PdfReader(counterclockwise["output_path"]).pages[0].rotation, 270)

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
