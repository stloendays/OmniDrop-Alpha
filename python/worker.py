#!/usr/bin/env python3
"""OmniDrop Python worker.

Protocol v1: one JSON request on stdin, one JSON response on stdout.
The worker stays stateless; the C++ host owns UI state, lifecycle, history,
cancellation policy, and user-facing semantics.
"""

from __future__ import annotations

import hashlib
import json
import stat
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any


WORKER_DIR = Path(__file__).resolve().parent
VENDOR_DIR = WORKER_DIR / "vendor"
if VENDOR_DIR.is_dir():
    sys.path.insert(0, str(VENDOR_DIR))


def ok(**payload: Any) -> dict[str, Any]:
    return {"ok": True, **payload}


def fail(message: str, code: str = "worker_error") -> dict[str, Any]:
    return {"ok": False, "error": {"code": code, "message": message}}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def unique_output_path(source: Path, marker: str, suffix: str | None = None) -> Path:
    output_suffix = source.suffix if suffix is None else suffix
    candidate = source.with_name(f"{source.stem}.{marker}{output_suffix}")
    counter = 2
    while candidate.exists():
        candidate = source.with_name(f"{source.stem}.{marker}-{counter}{output_suffix}")
        counter += 1
    return candidate


def normalize_text(path: Path) -> Path:
    content = path.read_text(encoding="utf-8")
    normalized = content.replace("\r\n", "\n").replace("\r", "\n")
    normalized = "\n".join(line.rstrip() for line in normalized.split("\n"))
    if normalized and not normalized.endswith("\n"):
        normalized += "\n"
    output = unique_output_path(path, "normalized")
    output.write_text(normalized, encoding="utf-8", newline="\n")
    return output


def deduplicate_lines(path: Path) -> Path:
    content = path.read_text(encoding="utf-8")
    seen: set[str] = set()
    kept: list[str] = []
    for line in content.splitlines():
        if line not in seen:
            seen.add(line)
            kept.append(line)
    output = unique_output_path(path, "deduplicated")
    text = "\n".join(kept)
    if text:
        text += "\n"
    output.write_text(text, encoding="utf-8", newline="\n")
    return output


def format_json(path: Path) -> Path:
    content = path.read_text(encoding="utf-8-sig")
    parsed = json.loads(content)
    output = unique_output_path(path, "formatted")
    rendered = json.dumps(parsed, ensure_ascii=False, indent=2)
    output.write_text(rendered + "\n", encoding="utf-8", newline="\n")
    return output


def format_xml(path: Path) -> Path:
    tree = ET.parse(path)
    ET.indent(tree, space="  ")
    output = unique_output_path(path, "formatted")
    tree.write(output, encoding="utf-8", xml_declaration=True, short_empty_elements=True)
    return output


def pillow_available() -> bool:
    try:
        import PIL  # noqa: F401
    except ImportError:
        return False
    return True


def image_compress(path: Path) -> Path:
    from PIL import Image

    with Image.open(path) as image:
        image.load()
        fmt = (image.format or path.suffix.lstrip(".")).upper()
        output = unique_output_path(path, "compressed")
        save_args: dict[str, Any] = {"optimize": True}

        if fmt in {"JPEG", "JPG"}:
            save_args["quality"] = 82
            save_args["progressive"] = True
        elif fmt == "WEBP":
            save_args["quality"] = 82
            save_args["method"] = 6
        elif fmt == "PNG":
            save_args["compress_level"] = 9

        # Compression should not silently become a metadata-removal action.
        for metadata_key in ("exif", "icc_profile"):
            value = image.info.get(metadata_key)
            if value:
                save_args[metadata_key] = value

        image.save(output, format=fmt, **save_args)
        return output


def image_convert_webp(path: Path) -> Path:
    from PIL import Image

    with Image.open(path) as image:
        image.load()
        output = unique_output_path(path, "converted", ".webp")
        save_args: dict[str, Any] = {"format": "WEBP", "quality": 82, "method": 6}
        for metadata_key in ("exif", "icc_profile"):
            value = image.info.get(metadata_key)
            if value:
                save_args[metadata_key] = value
        image.save(output, **save_args)
        return output


def image_remove_metadata(path: Path) -> Path:
    from PIL import Image, ImageOps

    with Image.open(path) as source:
        # Apply EXIF orientation before dropping metadata so visual orientation is preserved.
        image = ImageOps.exif_transpose(source)
        image.load()
        fmt = (source.format or path.suffix.lstrip(".")).upper()
        output = unique_output_path(path, "clean")
        save_args: dict[str, Any] = {}
        if fmt in {"JPEG", "JPG"}:
            if image.mode not in {"RGB", "L"}:
                image = image.convert("RGB")
            save_args.update(quality=95, optimize=True)
        elif fmt == "PNG":
            save_args.update(optimize=True)
        elif fmt == "WEBP":
            save_args.update(quality=95, method=6)
        image.save(output, format=fmt, **save_args)
        return output


def pypdf_available() -> bool:
    try:
        import pypdf  # noqa: F401
    except ImportError:
        return False
    return True


def pdf_extract_text(path: Path) -> Path:
    from pypdf import PdfReader

    reader = PdfReader(str(path))
    pages: list[str] = []
    for index, page in enumerate(reader.pages, start=1):
        text = page.extract_text() or ""
        pages.append(f"--- Page {index} ---\n{text.rstrip()}".rstrip())
    output = unique_output_path(path, "text", ".txt")
    body = "\n\n".join(pages)
    if body:
        body += "\n"
    output.write_text(body, encoding="utf-8", newline="\n")
    return output


def unique_output_directory(source: Path, marker: str) -> Path:
    candidate = source.with_name(f"{source.stem}.{marker}")
    counter = 2
    while candidate.exists():
        candidate = source.with_name(f"{source.stem}.{marker}-{counter}")
        counter += 1
    candidate.mkdir(parents=False)
    return candidate


def pdf_split(path: Path) -> tuple[Path, int]:
    from pypdf import PdfReader, PdfWriter

    reader = PdfReader(str(path))
    output_dir = unique_output_directory(path, "pages")
    for index, page in enumerate(reader.pages, start=1):
        writer = PdfWriter()
        writer.add_page(page)
        with (output_dir / f"page-{index:03d}.pdf").open("wb") as handle:
            writer.write(handle)
    return output_dir, len(reader.pages)



def pdf_merge(paths: list[Path]) -> tuple[Path, int]:
    from pypdf import PdfWriter

    if len(paths) < 2:
        raise ValueError("PDF merge requires at least two input files.")

    writer = PdfWriter()
    for path in paths:
        writer.append(str(path))

    output = unique_output_path(paths[0], "merged", ".pdf")
    page_count = len(writer.pages)
    with output.open("wb") as handle:
        writer.write(handle)
    return output, page_count


def zip_inspect(path: Path) -> Path:
    output = unique_output_path(path, "contents", ".txt")
    lines = ["name\tuncompressed_bytes\tcompressed_bytes"]
    with zipfile.ZipFile(path, "r") as archive:
        for entry in archive.infolist():
            lines.append(f"{entry.filename}\t{entry.file_size}\t{entry.compress_size}")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return output


def _validate_zip_member(output_dir: Path, entry: zipfile.ZipInfo) -> None:
    mode = (entry.external_attr >> 16) & 0xFFFF
    if stat.S_ISLNK(mode):
        raise ValueError(f"Archive contains a symbolic link: {entry.filename}")

    target = (output_dir / entry.filename).resolve()
    root = output_dir.resolve()
    try:
        target.relative_to(root)
    except ValueError as exc:
        raise ValueError(f"Archive entry escapes the output directory: {entry.filename}") from exc


def zip_extract(path: Path) -> tuple[Path, int]:
    output_dir = unique_output_directory(path, "extracted")
    try:
        with zipfile.ZipFile(path, "r") as archive:
            entries = archive.infolist()
            for entry in entries:
                _validate_zip_member(output_dir, entry)
            archive.extractall(output_dir)
        return output_dir, len(entries)
    except Exception:
        # Only remove the directory when validation/extraction failed before a useful result exists.
        import shutil

        shutil.rmtree(output_dir, ignore_errors=True)
        raise


def capabilities() -> list[str]:
    actions = [
        "file.sha256",
        "text.normalize",
        "text.deduplicate",
        "text.format_json",
        "text.format_xml",
        "archive.inspect",
        "archive.extract",
    ]
    if pillow_available():
        actions.extend(
            [
                "image.compress",
                "image.convert_webp",
                "image.remove_metadata",
            ]
        )
    if pypdf_available():
        actions.extend(["pdf.extract_text", "pdf.split", "pdf.merge"])
    return actions


def handle(request: dict[str, Any]) -> dict[str, Any]:
    command = request.get("command")
    if command == "ping":
        return ok(worker="python", protocol=1)

    if command == "capabilities":
        return ok(actions=capabilities(), pillow=pillow_available(), pypdf=pypdf_available())

    if command == "run_batch":
        action_id = request.get("action_id")
        raw_paths = request.get("paths")
        if not isinstance(raw_paths, list) or not all(isinstance(value, str) for value in raw_paths):
            return fail("paths must be a list of file paths", "invalid_request")
        paths = [Path(value) for value in raw_paths]
        if len(paths) < 2:
            return fail("Batch action requires at least two files.", "invalid_request")
        if any(not path.is_file() for path in paths):
            return fail("One or more input files do not exist.", "not_found")

        if action_id == "pdf.merge":
            if any(path.suffix.lower() != ".pdf" for path in paths):
                return fail("PDF merge accepts only PDF files.", "invalid_request")
            if not pypdf_available():
                return fail("pypdf is not installed for PDF processing.", "missing_dependency")
            try:
                output, page_count = pdf_merge(paths)
                return ok(
                    action_id=action_id,
                    paths=[str(path) for path in paths],
                    output_path=str(output),
                    input_count=len(paths),
                    page_count=page_count,
                )
            except Exception as exc:
                return fail(f"PDF merge failed: {exc}", "pdf_error")

        return fail(f"Unsupported batch action: {action_id}", "unsupported_action")

    if command == "run":
        action_id = request.get("action_id")
        raw_path = request.get("path")
        if not isinstance(raw_path, str):
            return fail("Missing path", "invalid_request")
        path = Path(raw_path)
        if not path.is_file():
            return fail("File does not exist", "not_found")

        if action_id == "file.sha256":
            return ok(action_id=action_id, path=str(path), sha256=sha256_file(path))
        if action_id == "text.normalize":
            return ok(action_id=action_id, path=str(path), output_path=str(normalize_text(path)))
        if action_id == "text.deduplicate":
            return ok(action_id=action_id, path=str(path), output_path=str(deduplicate_lines(path)))
        if action_id in {"text.format_json", "text.format_xml"}:
            try:
                operation = {
                    "text.format_json": format_json,
                    "text.format_xml": format_xml,
                }[action_id]
                return ok(action_id=action_id, path=str(path), output_path=str(operation(path)))
            except (UnicodeError, json.JSONDecodeError, ET.ParseError) as exc:
                return fail(f"Formatting failed: {exc}", "format_error")
        if action_id == "archive.inspect":
            try:
                output = zip_inspect(path)
                return ok(action_id=action_id, path=str(path), output_path=str(output))
            except zipfile.BadZipFile as exc:
                return fail(f"Archive inspection failed: {exc}", "archive_error")
        if action_id == "archive.extract":
            try:
                output_dir, entry_count = zip_extract(path)
                return ok(
                    action_id=action_id,
                    path=str(path),
                    output_path=str(output_dir),
                    entry_count=entry_count,
                )
            except (zipfile.BadZipFile, ValueError) as exc:
                return fail(f"Archive extraction failed: {exc}", "archive_error")
        if action_id == "pdf.extract_text":
            if not pypdf_available():
                return fail("pypdf is not installed for PDF processing.", "missing_dependency")
            try:
                output = pdf_extract_text(path)
                return ok(action_id=action_id, path=str(path), output_path=str(output))
            except Exception as exc:
                return fail(f"PDF text extraction failed: {exc}", "pdf_error")
        if action_id == "pdf.split":
            if not pypdf_available():
                return fail("pypdf is not installed for PDF processing.", "missing_dependency")
            try:
                output_dir, page_count = pdf_split(path)
                return ok(
                    action_id=action_id,
                    path=str(path),
                    output_path=str(output_dir),
                    page_count=page_count,
                )
            except Exception as exc:
                return fail(f"PDF split failed: {exc}", "pdf_error")
        if action_id in {
            "image.compress",
            "image.convert_webp",
            "image.remove_metadata",
        }:
            if not pillow_available():
                return fail("Pillow is not installed for image processing.", "missing_dependency")
            try:
                operation = {
                    "image.compress": image_compress,
                    "image.convert_webp": image_convert_webp,
                    "image.remove_metadata": image_remove_metadata,
                }[action_id]
                return ok(action_id=action_id, path=str(path), output_path=str(operation(path)))
            except Exception as exc:
                return fail(f"Image processing failed: {exc}", "image_error")

        return fail(f"Unsupported action: {action_id}", "unsupported_action")

    return fail(f"Unsupported command: {command}", "unsupported_command")


def main() -> int:
    try:
        request = json.loads(sys.stdin.read())
        if not isinstance(request, dict):
            raise ValueError("Request must be a JSON object")
        response = handle(request)
    except Exception as exc:  # Boundary: convert worker failures to structured output.
        response = fail(str(exc))

    sys.stdout.write(json.dumps(response, ensure_ascii=False))
    sys.stdout.write("\n")
    return 0 if response.get("ok") else 2


if __name__ == "__main__":
    raise SystemExit(main())
