#!/usr/bin/env python3
"""Build the independently distributed OmniDrop English voice pack.

Downloads occur only when --fetch is provided. The model's public SHA-256
is checked before writing the ZIP. The runtime is intentionally separate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
import urllib.request
import zipfile
from pathlib import Path

VOICE_ID = "en_US-ljspeech-medium"
MODEL_FILE = VOICE_ID + ".onnx"
CONFIG_FILE = MODEL_FILE + ".json"
EXPECTED_SHA256 = "6f52a751e2349abe7a76735eb09dc1875298c77ea2342ffd2fef79ff81b87f22"
UPSTREAM = "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/ljspeech/medium/"
MAX_BYTES = 150_000_000

NOTICE = """OmniDrop optional English voice pack
Model: Piper en_US-ljspeech-medium
Provenance: https://huggingface.co/rhasspy/piper-voices
Dataset: LJ Speech, public domain (per upstream MODEL_CARD)
Piper voice repository: MIT, per upstream repository metadata.
Model is distributed as a separate asset, not committed into OmniDrop's source repository.
The Piper inference runtime is NOT included in this model-only voice pack.
Current Piper runtime distributions can be GPLv3; review the runtime license before redistribution.
See MODEL_CARD for upstream model training provenance and attribution.
"""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as data:
        for chunk in iter(lambda: data.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, target: Path) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "OmniDrop-voice-pack-builder"})
    amount = 0
    with urllib.request.urlopen(request, timeout=120) as response, target.open("wb") as out:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            amount += len(chunk)
            if amount > MAX_BYTES:
                raise ValueError("Upstream file exceeds the configured size cap.")
            out.write(chunk)


def build(source: Path, output: Path, expected_hash: str) -> Path:
    model = source / MODEL_FILE
    config = source / CONFIG_FILE
    model_card = source / "MODEL_CARD"
    if not model.is_file() or not config.is_file() or not model_card.is_file():
        raise ValueError("Voice pack requires ONNX model, model config JSON and MODEL_CARD.")
    if model.stat().st_size > MAX_BYTES:
        raise ValueError("Model file is larger than supported voice-pack limit.")
    digest = sha256(model)
    if digest != expected_hash:
        raise ValueError("Voice model SHA-256 did not match the pinned expected digest.")
    parsed = json.loads(config.read_text(encoding="utf-8"))
    if not isinstance(parsed, dict) or "audio" not in parsed:
        raise ValueError("Piper voice config is missing an audio section.")

    manifest = {
        "schema_version": 1,
        "voice_id": VOICE_ID,
        "language": "en-US",
        "runtime": "piper-tts",
        "model_file": MODEL_FILE,
        "config_file": CONFIG_FILE,
        "sha256": digest,
        "config_sha256": sha256(config),
        "source": UPSTREAM,
        "license_notice": "NOTICE.txt",
    }
    prefix = f"models/voice-packs/{VOICE_ID}/"
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=3) as archive:
        for name, source_file in [(MODEL_FILE, model), (CONFIG_FILE, config), ("MODEL_CARD", model_card)]:
            archive.write(source_file, prefix + name)
        archive.writestr(prefix + "manifest.json", json.dumps(manifest, indent=2) + "\n")
        archive.writestr(prefix + "NOTICE.txt", NOTICE)
    with zipfile.ZipFile(output) as archive:
        invalid = archive.testzip()
        if invalid:
            raise ValueError("Voice pack archive failed the ZIP integrity check.")
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fetch", action="store_true", help="Explicitly download verified upstream files")
    parser.add_argument("--source-dir", type=Path, help="Use local model/config/card files instead of network")
    parser.add_argument("--output", type=Path, default=Path("dist") / f"OmniDrop-VoicePack-{VOICE_ID}.zip")
    parser.add_argument("--expected-sha256", default=EXPECTED_SHA256)
    args = parser.parse_args()
    if args.fetch == bool(args.source_dir):
        parser.error("Choose exactly one of --fetch or --source-dir.")
    if not args.fetch:
        result = build(args.source_dir, args.output, args.expected_sha256)
    else:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            for filename in (MODEL_FILE, CONFIG_FILE, "MODEL_CARD"):
                download(UPSTREAM + filename, source / filename)
            result = build(source, args.output, args.expected_sha256)
    print(f"Built verified voice pack: {result}")


if __name__ == "__main__":
    main()
