from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import types
import unittest
import wave
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
sys.path.insert(0, str(ROOT / "scripts"))

import audio_voice
import build_voice_pack
import worker


def make_pack(root: Path, data: bytes = b"mock-model") -> Path:
    folder = root / "voice-packs" / build_voice_pack.VOICE_ID
    folder.mkdir(parents=True)
    (folder / build_voice_pack.MODEL_FILE).write_bytes(data)
    (folder / build_voice_pack.CONFIG_FILE).write_text(
        '{"audio":{"sample_rate":22050}}', encoding="utf-8")
    (folder / "MODEL_CARD").write_text("# Local test card", encoding="utf-8")
    (folder / "manifest.json").write_text(
        json.dumps({
            "schema_version": 1,
            "voice_id": build_voice_pack.VOICE_ID,
            "language": "en-US",
            "model_file": build_voice_pack.MODEL_FILE,
            "config_file": build_voice_pack.CONFIG_FILE,
            "sha256": hashlib.sha256(data).hexdigest(),
        }), encoding="utf-8")
    return folder


class VoiceTests(unittest.TestCase):
    def test_missing_voice_is_not_advertised_as_available(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict("os.environ", {"OMNIDROP_VOICE_PACK_DIR": directory}):
                with patch.object(audio_voice, "piper_available", return_value=True):
                    self.assertEqual(audio_voice.installed_voices(), [])
                    self.assertFalse(audio_voice.speech_available())
                    self.assertNotIn("text.to_speech", worker.capabilities())

    def test_tampered_model_pack_rejected_by_sha256(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = make_pack(root)
            with patch.dict("os.environ", {"OMNIDROP_VOICE_PACK_DIR": str(root / "voice-packs")}):
                self.assertEqual(len(audio_voice.installed_voices()), 1)
                (folder / build_voice_pack.MODEL_FILE).write_bytes(b"tampered")
                self.assertEqual(audio_voice.installed_voices(), [])

    def test_successful_wav_has_valid_audio_and_preserves_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            make_pack(root)
            source = root / "chapter.txt"
            source.write_text("Hello audio.", encoding="utf-8")
            original = source.read_bytes()

            calls = []
            class FakeVoice:
                def synthesize_wav(self, text, wav_file):
                    calls.append(text)
                    wav_file.setnchannels(1)
                    wav_file.setsampwidth(2)
                    wav_file.setframerate(22050)
                    wav_file.writeframes(b"\x00\x00" * 100)

            fake_piper = types.ModuleType("piper")
            fake_piper.PiperVoice = types.SimpleNamespace(load=lambda model: FakeVoice())
            with patch.dict(sys.modules, {"piper": fake_piper}):
                with patch.dict("os.environ", {"OMNIDROP_VOICE_PACK_DIR": str(root / "voice-packs")}):
                    with patch.object(audio_voice, "piper_available", return_value=True):
                        first = worker.handle({
                            "command": "run", "action_id": "text.to_speech", "path": str(source)
                        })
                        second = worker.handle({
                            "command": "run", "action_id": "text.to_speech", "path": str(source)
                        })
            self.assertTrue(first["ok"], first)
            self.assertTrue(second["ok"], second)
            self.assertNotEqual(first["output_path"], second["output_path"])
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(calls, ["Hello audio.", "Hello audio."])
            with wave.open(first["output_path"], "rb") as wav:
                self.assertEqual(wav.getframerate(), 22050)
                self.assertEqual(wav.getnframes(), 100)
            self.assertFalse(first["remote"])

    def test_subtitles_read_only_dialogue_not_cues(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "clip.srt"
            source.write_text("1\n00:00:01,000 --> 00:00:03,000\nHello\n\n2\n00:00:05,000 --> 00:00:07,000\nWorld\n", encoding="utf-8")
            self.assertEqual(audio_voice._extract_speech_text(source), "Hello World")

    def test_build_voice_pack_from_verified_local_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = make_pack(root)
            archive = root / "voice-pack.zip"
            expected = hashlib.sha256(b"mock-model").hexdigest()
            build_voice_pack.build(folder, archive, expected)
            with zipfile.ZipFile(archive) as packaged:
                self.assertIsNone(packaged.testzip())
                names = packaged.namelist()
                self.assertIn(
                    f"models/voice-packs/{build_voice_pack.VOICE_ID}/manifest.json",
                    names)
                self.assertIn(
                    f"models/voice-packs/{build_voice_pack.VOICE_ID}/NOTICE.txt",
                    names)
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                build_voice_pack.build(folder, root / "bad.zip", "0" * 64)

    def test_invalid_voice_pack_name_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = make_pack(root)
            manifest_path = folder / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["model_file"] = "../escape.onnx"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with patch.dict("os.environ", {"OMNIDROP_VOICE_PACK_DIR": str(root / "voice-packs")}):
                self.assertEqual(audio_voice.installed_voices(), [])


if __name__ == "__main__":
    unittest.main()
