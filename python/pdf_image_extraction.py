"""Bounded offline extraction of embedded PDF images.

The extractor never renders pages, runs OCR, follows PDF-derived paths or
changes the input PDF. It writes a separate output directory.
"""
from __future__ import annotations

import shutil
from pathlib import Path
from typing import Callable

MAX_PAGES = 2000
MAX_IMAGES = 512
MAX_IMAGE_BYTES = 64 * 1024 * 1024
MAX_TOTAL_BYTES = 256 * 1024 * 1024
SAFE_EXTENSIONS = frozenset({
    ".png", ".jpg", ".jpeg", ".jp2", ".bmp", ".tif", ".tiff", ".webp",
})


def extract_images(
    path: Path,
    allocate_directory: Callable[[Path, str], Path],
) -> tuple[Path, int]:
    """Extract image streams as page-indexed files without overwriting inputs."""
    from pypdf import PdfReader

    reader = PdfReader(str(path))
    if len(reader.pages) > MAX_PAGES:
        raise ValueError(f"PDF exceeds {MAX_PAGES} page extraction limit.")

    output_dir: Path | None = None
    count = 0
    total = 0
    try:
        for page_number, page in enumerate(reader.pages, start=1):
            for image_index, image in enumerate(page.images, start=1):
                count += 1
                if count > MAX_IMAGES:
                    raise ValueError(f"PDF contains more than {MAX_IMAGES} embedded images.")
                data = image.data
                if not isinstance(data, bytes):
                    raise ValueError("PDF yielded an invalid image payload.")
                total += len(data)
                if len(data) > MAX_IMAGE_BYTES or total > MAX_TOTAL_BYTES:
                    raise ValueError("PDF images exceed extraction size limits.")

                if output_dir is None:
                    output_dir = allocate_directory(path, "images")

                # Never use filenames from inside an untrusted PDF as paths.
                extension = Path(image.name).suffix.lower()
                if extension not in SAFE_EXTENSIONS:
                    extension = ".bin"
                name = f"page-{page_number:04d}-image-{image_index:03d}{extension}"
                with (output_dir / name).open("xb") as destination:
                    destination.write(data)

        if output_dir is None:
            output_dir = allocate_directory(path, "images")
        return output_dir, count
    except Exception:
        if output_dir is not None:
            shutil.rmtree(output_dir, ignore_errors=True)
        raise
