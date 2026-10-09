"""Verify README-facing SVG artwork is portable, safe and self-contained."""

from __future__ import annotations

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMAGES = [
    ROOT / "assets/readme/desktop-preview.svg",
    ROOT / "assets/readme/workflow-preview.svg",
    ROOT / "assets/readme/jobs-preview.svg",
    ROOT / "assets/readme/social-preview.svg",
]


class ReadmeVisualTests(unittest.TestCase):
    def test_local_svg_assets_are_valid_and_self_contained(self):
        for path in IMAGES:
            with self.subTest(file=path.name):
                self.assertTrue(path.is_file())
                content = path.read_text("utf-8")
                root = ET.fromstring(content)
                self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg")
                self.assertLess(path.stat().st_size, 50_000)
                self.assertNotIn("<script", content.lower())
                self.assertNotIn("<foreignObject", content)
                self.assertNotIn("http://www.w3.org/1999/xlink", content)
                self.assertNotIn("data:image/", content)
                self.assertNotIn("file://", content)
                self.assertNotIn("https://", content)
                self.assertNotIn("onload=", content.lower())
                self.assertTrue(root.attrib.get("aria-label"))
                for node in root.iter():
                    for key, value in node.attrib.items():
                        self.assertFalse(key.lower().startswith("on"))
                        self.assertNotIn("url(http", value.lower())


if __name__ == "__main__":
    unittest.main()
