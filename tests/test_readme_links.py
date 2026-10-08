"""Keep the bilingual GitHub landing pages navigable as the product evolves.

Uses only the Python standard library so the check works in core CI on
Windows and Linux. It checks local targets; network links are intentionally
not fetched in the test suite.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
LANDING_PAGES = [
    ROOT / "README.md",
    ROOT / "docs" / "README.zh-CN.md",
]
MARKDOWN_LINK = re.compile(r"!?(?:\[[^\]]*\])\(([^)]+)\)")
HTML_HREF = re.compile(r'<a\s+[^>]*?href="([^"]+)"', re.IGNORECASE)
VERSION = re.compile(r"^project\(OmniDrop VERSION (\d+\.\d+\.\d+) LANGUAGES CXX\)$", re.MULTILINE)


def local_links(document: str) -> list[str]:
    matches = [match.group(1) for match in MARKDOWN_LINK.finditer(document)]
    matches.extend(match.group(1) for match in HTML_HREF.finditer(document))
    return matches


class ReadmeIntegrityTests(unittest.TestCase):
    def test_no_broken_relative_links(self) -> None:
        for document in LANDING_PAGES:
            text = document.read_text(encoding="utf-8")
            for value in local_links(text):
                with self.subTest(document=document.name, target=value):
                    link = value.strip().split(" ", 1)[0]
                    if not link or link.startswith("#"):
                        continue
                    split = urlsplit(link)
                    if split.scheme or split.netloc:
                        continue
                    # Remove any anchor before checking the repo-local path.
                    resolved = (document.parent / unquote(split.path)).resolve()
                    self.assertTrue(
                        resolved.is_relative_to(ROOT),
                        f"Local link escapes repository: {link}",
                    )
                    self.assertTrue(
                        resolved.exists(),
                        f"{document.relative_to(ROOT)} links to missing {link}",
                    )

    def test_public_version_matches_cmake(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        version_match = VERSION.search(cmake)
        self.assertIsNotNone(version_match, "CMake project version missing")
        version = version_match.group(1)
        for document in LANDING_PAGES:
            with self.subTest(document=document.name):
                self.assertIn(f"v{version}", document.read_text(encoding="utf-8"))

    def test_future_visuals_never_use_broken_media_paths(self) -> None:
        # A real image should be committed before the README references it.
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        for match in re.finditer(r"!\[[^\]]*\]\(([^)]+)\)", readme):
            target = match.group(1).split("#", 1)[0]
            if target.startswith(("https://", "http://")):
                continue
            with self.subTest(image=target):
                self.assertTrue((ROOT / unquote(target)).is_file())


if __name__ == "__main__":
    unittest.main()
