"""Bounded, offline previews and source provenance for MinerU result artifacts."""

from __future__ import annotations

import hashlib
import json
import zipfile
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

_MAX_TEXT = 200_000
_MAX_PREVIEW = 48_000
_MAX_IMAGE = 2_000_000
_MAX_IMAGES = 8
_SAFE_IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".webp", ".gif"}


class PreviewError(ValueError):
    """MinerU returned an invalid or unusable result artifact."""


def _source_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _middle_stats(value: object) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("schema") != "docvortex.middle":
        raise PreviewError("Expected MinerU 4 docvortex.middle schema")
    if value.get("schema_version") != "2.0":
        raise PreviewError("Unsupported MinerU MiddleJson version")
    pages = value.get("pages")
    if not isinstance(pages, list):
        raise PreviewError("MiddleJson pages must be an array")
    counts: Counter[str] = Counter()
    indices = set()
    for page in pages:
        if not isinstance(page, dict) or not isinstance(page.get("blocks"), list):
            raise PreviewError("MiddleJson page must have blocks array")
        index = page.get("page_idx")
        if not isinstance(index, int) or isinstance(index, bool) or index < 0 or index in indices:
            raise PreviewError("MiddleJson page indices must be unique nonnegative integers")
        indices.add(index)
        for block in page["blocks"]:
            if not isinstance(block, dict) or not isinstance(block.get("type"), str):
                raise PreviewError("MiddleJson blocks require a type")
            counts[block["type"]] += 1
    return {
        "page_count": len(pages),
        "page_indices": sorted(indices),
        "block_types": dict(sorted(counts.items())),
        "is_full_document": value.get("is_full_document") is True,
    }


def _safe_markdown(artifact: Path) -> str:
    if artifact.stat().st_size > _MAX_TEXT:
        with artifact.open("rb") as handle:
            return handle.read(_MAX_PREVIEW).decode("utf-8", errors="replace")
    return artifact.read_text(encoding="utf-8-sig")[:_MAX_PREVIEW]


def _zip_preview(artifact: Path, directory: Path) -> tuple[str, dict[str, Any], list[Path]]:
    images: list[Path] = []
    with zipfile.ZipFile(artifact) as archive:
        members = archive.infolist()
        if len(members) > 30_000:
            raise PreviewError("Result ZIP contains too many members")
        names = {entry.filename: entry for entry in members}
        middle = names.get("middle_json.json")
        markdown = names.get("markdown.md")
        if middle is None or markdown is None:
            raise PreviewError("MinerU 4 result ZIP is missing markdown.md or middle_json.json")
        if middle.file_size > 32 * 1024 * 1024 or markdown.file_size > 8 * 1024 * 1024:
            raise PreviewError("Result JSON or Markdown exceeds preview limits")
        try:
            obj = json.loads(archive.read(middle).decode("utf-8"))
        except (ValueError, UnicodeError) as exc:
            raise PreviewError("Result ZIP contains invalid MiddleJson") from exc
        stats = _middle_stats(obj)
        content = archive.read(markdown)[:_MAX_PREVIEW].decode("utf-8", errors="replace")
        preview_dir = directory / "preview_images"
        for entry in members:
            if len(images) == _MAX_IMAGES:
                break
            # ZIP member paths are never used as filesystem output paths.
            if not entry.filename.startswith("images/") or entry.is_dir():
                continue
            suffix = Path(entry.filename).suffix.lower()
            if suffix not in _SAFE_IMAGE_EXTENSIONS or not 0 < entry.file_size <= _MAX_IMAGE:
                continue
            if entry.file_size > 100 * max(entry.compress_size, 1):
                continue
            preview_dir.mkdir(exist_ok=True)
            output = preview_dir / f"figure_{len(images) + 1:02d}{suffix}"
            with archive.open(entry) as handle, output.open("wb") as target:
                remaining = _MAX_IMAGE + 1
                while remaining > 0:
                    chunk = handle.read(min(remaining, 65536))
                    if not chunk:
                        break
                    target.write(chunk)
                    remaining -= len(chunk)
            if output.stat().st_size > _MAX_IMAGE:
                output.unlink(missing_ok=True)
                continue
            images.append(output)
        return content, stats, images


def prepare_scientific_preview(source: Path, artifact: Path, fmt: str, directory: Path,
                               tier: str = "standard") -> dict[str, Any]:
    """Write preview and provenance report without modifying the source PDF."""
    stats: dict[str, Any] = {}
    images: list[Path] = []
    if fmt == "middle_json":
        try:
            raw = json.loads(artifact.read_text(encoding="utf-8"))
        except (ValueError, UnicodeError) as exc:
            raise PreviewError("MinerU output is not valid UTF-8 JSON") from exc
        stats = _middle_stats(raw)
        excerpt = json.dumps(raw.get("pages", [])[:2], ensure_ascii=False, indent=2)[:_MAX_PREVIEW]
    elif fmt == "markdown":
        excerpt = _safe_markdown(artifact)
    elif fmt == "zip":
        excerpt, stats, images = _zip_preview(artifact, directory)
    else:
        raise PreviewError("Unknown MinerU result format")
    if not excerpt.strip() and not stats.get("page_count"):
        raise PreviewError("MinerU returned an empty document")
    provenance = {
        "schema_version": 1,
        "source_name": source.name,
        "source_sha256": _source_sha256(source),
        "source_bytes": source.stat().st_size,
        "processed_at_utc": datetime.now(timezone.utc).isoformat(),
        "engine": "mineru-kit",
        "requested_tier": tier,
        "output_format": fmt,
        "result_name": artifact.name,
        "validation_status": "not_manually_verified",
        "numeric_values_verified": False,
        **stats,
    }
    provenance_path = directory / "provenance.json"
    provenance_path.write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    blocks = ", ".join(f"{kind}: {count}" for kind, count in stats.get("block_types", {}).items())
    preview = (
        "# Scientific PDF extraction preview\n\n"
        f"Source: {source.name}\n"
        f"SHA-256: {provenance['source_sha256']}\n"
        f"Format: {fmt}; tier requested: {tier}\n"
        f"Pages represented: {stats.get('page_count', 'unknown')}\n"
        f"Blocks: {blocks or 'not available in standalone Markdown'}\n\n"
        "IMPORTANT: Extracted content and numerical values have NOT been manually verified. "
        "Check the original PDF and Supporting Information before citing results.\n\n"
        "## Extracted text (bounded preview; equations show LaTeX source)\n\n"
        + excerpt + "\n"
    )
    preview_path = directory / "preview.md"
    preview_path.write_text(preview, encoding="utf-8")
    return {
        "preview_path": str(preview_path),
        "provenance_path": str(provenance_path),
        "preview_images": [str(path) for path in images],
        "page_count": stats.get("page_count"),
    }
