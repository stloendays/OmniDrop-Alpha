"""Optional local MinerU 4 scientific PDF adapter."""
import json
import os
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path
from scientific_preview import prepare_scientific_preview, PreviewError

FORMATS = {
    "pdf.parse_scientific": ("markdown", ".md"),
    "pdf.parse_scientific_json": ("middle_json", ".json"),
    "pdf.export_scientific_bundle": ("zip", ".zip"),
}

class MineruError(Exception):
    def __init__(self, message, code="mineru_error"):
        super().__init__(message)
        self.code = code

def find_cli():
    explicit = os.getenv("OMNIDROP_MINERU_CLI")
    if explicit is not None:
        if not explicit.strip():
            return None
        name = explicit.strip().strip('"')
        return name if Path(name).is_file() else shutil.which(name)
    return shutil.which("mineru-kit")

def available():
    return find_cli() is not None

def parse(path, action_id):
    if action_id not in FORMATS:
        raise MineruError("Unsupported MinerU action", "unsupported_action")
    if not path.is_file() or path.suffix.lower() != ".pdf":
        raise MineruError("A local PDF file is required", "invalid_request")
    executable = find_cli()
    if not executable:
        raise MineruError("Install MinerU 4 separately and configure OMNIDROP_MINERU_CLI if needed.", "missing_dependency")
    fmt, extension = FORMATS[action_id]
    try:
        timeout = int(os.getenv("OMNIDROP_MINERU_TIMEOUT_SECONDS", "1800"))
        if not 60 <= timeout <= 1800:
            raise ValueError()
    except ValueError as exc:
        raise MineruError("Timeout must be between 60 and 1800 seconds.", "invalid_configuration") from exc
    stage = Path(tempfile.mkdtemp(prefix=".omnidrop-mineru-", dir=str(path.parent)))
    output = stage / (path.stem + extension)
    try:
        environment = os.environ.copy()
        environment.pop("MINERU_API_URL", None)
        environment.pop("MINERU_API_KEY", None)
        command = [executable, "parse", str(path.resolve()), "-o", str(output),
                   "--tier", "standard", "--format", fmt]
        try:
            process = subprocess.run(command, stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                    timeout=timeout, env=environment, check=False)
        except subprocess.TimeoutExpired as exc:
            raise MineruError("MinerU parsing timed out.", "timeout") from exc
        except OSError as exc:
            raise MineruError("Failed to launch mineru-kit.", "missing_dependency") from exc
        if process.returncode:
            raise MineruError("MinerU returned exit status " + str(process.returncode)
                              + ": " + (process.stderr or process.stdout)[-400:])
        if not output.is_file() or output.stat().st_size == 0:
            raise MineruError("No output generated.", "missing_output")
        try:
            if fmt == "middle_json":
                obj = json.loads(output.read_text(encoding="utf-8"))
                if not isinstance(obj, dict) or obj.get("schema") != "docvortex.middle":
                    raise ValueError("Invalid MiddleJson schema")
            elif fmt == "zip" and not zipfile.is_zipfile(output):
                raise ValueError("Invalid ZIP")
        except (ValueError, UnicodeError) as exc:
            raise MineruError("Invalid MinerU output.", "invalid_output") from exc
        try:
            preview = prepare_scientific_preview(path, output, fmt, stage)
        except (PreviewError, zipfile.BadZipFile, OSError) as exc:
            raise MineruError(f"MinerU preview/provenance validation failed: {exc}", "invalid_output") from exc
        base = path.parent / (path.stem + ".mineru-" + fmt)
        destination = base
        number = 2
        while destination.exists():
            destination = path.parent / (base.name + "-" + str(number))
            number += 1
        stage.rename(destination)
        return {
            "output_path": str(destination / output.name),
            "output_dir": str(destination),
            "format": fmt,
            "engine": "mineru-kit",
            "preview_path": str(destination / Path(preview["preview_path"]).relative_to(stage)),
            "provenance_path": str(destination / Path(preview["provenance_path"]).relative_to(stage)),
            "preview_images": [
                str(destination / Path(image).relative_to(stage))
                for image in preview["preview_images"]
            ],
            "page_count": preview["page_count"],
        }
    finally:
        if stage.exists():
            shutil.rmtree(stage)
