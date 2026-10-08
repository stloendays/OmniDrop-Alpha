"""Local-only neural text-to-speech through user-installed Piper voice packs.

No cloud services, implicit downloads or account requirements. The model is
loaded only if a verified voice pack and an optional Piper runtime are present.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import re
import tempfile
import wave
from pathlib import Path
from typing import Any

ALLOWED_SUFFIXES = {".txt", ".md", ".markdown", ".srt", ".vtt"}
VOICE_ID_PATTERN = re.compile(r"^[a-zA-Z0-9_.-]{1,64}$")
MAX_INPUT_BYTES = 100_000
MAX_SPEECH_CHARS = 8000


class SpeechError(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def _pack_base() -> Path:
    override = os.getenv("OMNIDROP_VOICE_PACK_DIR", "").strip()
    if override:
        return Path(override)
    return Path(__file__).resolve().parents[1] / "models" / "voice-packs"


def _check_name(filename: str, suffix: str) -> bool:
    return filename == Path(filename).name and filename.endswith(suffix) and "/" not in filename and "\\" not in filename


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(part)
    return digest.hexdigest()


def installed_voices() -> list[dict[str, Any]]:
    base = _pack_base()
    voices: list[dict[str, Any]] = []
    if not base.is_dir():
        return voices

    # Avoid arbitrary-depth scanning of a potentially huge model directory.
    for manifest_path in sorted(base.glob("*/manifest.json"))[:32]:
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            voice_id = manifest["voice_id"]
            model_name = manifest["model_file"]
            config_name = manifest["config_file"]
            digest = manifest["sha256"]
            if not all(isinstance(v, str) for v in (voice_id, model_name, config_name, digest)):
                continue
            if not VOICE_ID_PATTERN.fullmatch(voice_id):
                continue
            if voice_id != manifest_path.parent.name:
                continue
            if not _check_name(model_name, ".onnx") or not _check_name(config_name, ".onnx.json"):
                continue
            if not re.fullmatch(r"[0-9a-f]{64}", digest):
                continue
            model = manifest_path.parent / model_name
            config = manifest_path.parent / config_name
            if not model.is_file() or not config.is_file() or model.stat().st_size > 150_000_000:
                continue
            if _sha256(model) != digest:
                continue
            parsed = json.loads(config.read_text(encoding="utf-8"))
            if not isinstance(parsed, dict) or "audio" not in parsed:
                continue
            voices.append({
                "voice_id": voice_id, "model_path": str(model),
                "language": str(manifest.get("language", "")),
            })
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return voices


def piper_available() -> bool:
    try:
        return importlib.util.find_spec("piper") is not None
    except (ValueError, ImportError):
        return False


def speech_available() -> bool:
    return piper_available() and bool(installed_voices())


def _extract_speech_text(path: Path) -> str:
    if path.suffix.lower() not in ALLOWED_SUFFIXES:
        raise SpeechError("unsupported_format", "Voice narration supports UTF-8 TXT, MD, SRT and VTT.")
    if not path.is_file():
        raise SpeechError("not_found", "Input text file does not exist.")
    if path.stat().st_size > MAX_INPUT_BYTES:
        raise SpeechError("file_too_large", "Input file exceeds the 100 KB first-version limit.")
    try:
        data = path.read_text(encoding="utf-8-sig")
    except UnicodeError:
        raise SpeechError("invalid_encoding", "Input file must be UTF-8.") from None

    suffix = path.suffix.lower()
    lines: list[str] = []
    code_block = False
    for line in data.splitlines():
        stripped = line.strip()
        if suffix in {".srt", ".vtt"}:
            if not stripped or stripped.isdecimal() or "-->" in stripped or stripped == "WEBVTT":
                continue
        if suffix in {".md", ".markdown"}:
            if stripped.startswith(("```", "~~~")):
                code_block = not code_block
                continue
            if code_block or not stripped or stripped.startswith(("![", "|", "<!--")):
                continue
            stripped = re.sub(r"^(?:#{1,6}|>|[-*+]|\d+[.)])\s+", "", stripped)
        if stripped:
            lines.append(stripped)

    result = " ".join(lines)
    if not result:
        raise SpeechError("empty_input", "No narratable text was found.")
    if len(result) > MAX_SPEECH_CHARS:
        raise SpeechError("text_too_long", "Narration is limited to 8,000 characters per file.")
    return result


def _unique_wav(path: Path, index: int = 1) -> Path:
    label = ".spoken" if index == 1 else f".spoken-{index}"
    return path.with_name(f"{path.stem}{label}.wav")


def narrate_file(path: Path, voice_id: str = "") -> dict[str, Any]:
    text = _extract_speech_text(path)
    if not piper_available():
        raise SpeechError("missing_dependency", "Piper TTS is not installed in the local Python environment.")

    voices = installed_voices()
    if not voices:
        raise SpeechError("voice_pack_missing", "No verified voice pack is installed. Add an OmniDrop Voice Pack.")
    selected = voice_id or os.getenv("OMNIDROP_VOICE_ID", "") or voices[0]["voice_id"]
    chosen = next((voice for voice in voices if voice["voice_id"] == selected), None)
    if chosen is None:
        raise SpeechError("voice_not_found", "Requested voice pack was not found or failed integrity verification.")

    try:
        from piper import PiperVoice
    except ImportError:
        raise SpeechError("missing_dependency", "Installed Piper TTS runtime could not be loaded.") from None

    output_index = 1
    destination = _unique_wav(path, output_index)
    stage_name = None
    try:
        # A temp file in the same directory allows atomic completion of a full WAV.
        with tempfile.NamedTemporaryFile(prefix=".omnidrop-voice-", suffix=".wav",
                                         dir=path.parent, delete=False) as temp:
            stage_name = temp.name
        engine = PiperVoice.load(chosen["model_path"])
        with wave.open(stage_name, "wb") as stream:
            engine.synthesize_wav(text, stream)

        if Path(stage_name).stat().st_size <= 44:
            raise SpeechError("empty_audio", "Voice engine returned an empty WAV.")

        # Hard-linking creates a finished output without overwriting an existing file.
        while True:
            try:
                os.link(stage_name, destination)
                break
            except FileExistsError:
                output_index += 1
                destination = _unique_wav(path, output_index)
        return {
            "output_path": str(destination),
            "voice_id": chosen["voice_id"],
            "characters_spoken": len(text),
            "remote": False,
        }
    except SpeechError:
        raise
    except Exception:
        raise SpeechError("speech_error", "Offline voice synthesis failed. No output was saved.") from None
    finally:
        if stage_name is not None:
            Path(stage_name).unlink(missing_ok=True)
