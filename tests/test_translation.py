"""Translation contract tests without making any network requests."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

import translation
import worker


class TranslationTests(unittest.TestCase):
    def new_file(self, folder: Path, name: str, content: str) -> Path:
        path = folder / name
        path.write_text(content, encoding="utf-8")
        return path

    def request(self, file: Path, provider: str = "mymemory", remote: bool = False) -> dict:
        return {
            "command": "translate_file",
            "path": str(file),
            "source_lang": "en",
            "target_lang": "zh",
            "provider": provider,
            "allow_remote": remote,
        }

    def test_remote_requires_explicit_consent_without_network(self):
        with tempfile.TemporaryDirectory() as directory:
            file = self.new_file(Path(directory), "private.txt", "CONFIDENTIAL")
            with patch.object(translation, "_fetch_json", side_effect=AssertionError("unexpected request")):
                result = worker.handle(self.request(file))
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "consent_required")
            self.assertEqual(list(Path(directory).glob("*.translated*")), [])

    def test_mymemory_translates_and_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            file = self.new_file(root, "note.txt", "Hello world\n\nNext line\n")
            original = file.read_bytes()
            sent = []
            def fake_request(request):
                query = dict(translation.urllib.parse.parse_qsl(
                    translation.urllib.parse.urlsplit(request.full_url).query))
                sent.append(query)
                return {"responseStatus": 200, "responseData": {"translatedText": "你好"}}

            with patch.object(translation, "_fetch_json", side_effect=fake_request):
                result = worker.handle(self.request(file, remote=True))

            self.assertTrue(result["ok"], result)
            self.assertEqual(file.read_bytes(), original)
            self.assertEqual(Path(result["output_path"]).read_text(encoding="utf-8"), "你好\n\n你好\n")
            self.assertEqual(result["segments_translated"], 2)
            self.assertEqual(result["characters_transmitted"], len("Hello world") + len("Next line"))
            self.assertEqual([item["langpair"] for item in sent], ["en|zh"] * 2)
            self.assertNotIn("CONFIDENTIAL", json.dumps(result))

    def test_utf8_chunks_respect_mymemory_500_byte_limit(self):
        segments = translation._segments("中" * 350, 450)
        self.assertGreater(len(segments), 1)
        self.assertEqual("".join(segments), "中" * 350)
        self.assertTrue(all(len(segment.encode("utf-8")) <= 450 for segment in segments))

    def test_markdown_code_fences_and_heading_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            file = self.new_file(Path(directory), "readme.md",
                "# Hello\n\n```python\nprint('hi')\n```\n- Document\n")
            with patch.object(translation, "_translate_one", side_effect=lambda t,*_: "TR:" + t):
                result = worker.handle(self.request(file, remote=True))
            self.assertTrue(result["ok"], result)
            content = Path(result["output_path"]).read_text(encoding="utf-8")
            self.assertIn("# TR:Hello\n", content)
            self.assertIn("```python\nprint('hi')\n```\n", content)
            self.assertIn("- TR:Document\n", content)

    def test_subtitles_keep_timestamps_and_indices(self):
        with tempfile.TemporaryDirectory() as directory:
            file = self.new_file(Path(directory), "clip.srt",
                "1\n00:00:01,000 --> 00:00:03,000\nHello there\n\n2\n00:00:04,000 --> 00:00:06,000\nGoodbye\n")
            with patch.object(translation, "_translate_one", side_effect=lambda t,*_: "译:" + t):
                result = worker.handle(self.request(file, remote=True))
            self.assertTrue(result["ok"], result)
            content = Path(result["output_path"]).read_text(encoding="utf-8")
            self.assertIn("1\n00:00:01,000 --> 00:00:03,000\n译:Hello there\n", content)
            self.assertIn("2\n00:00:04,000 --> 00:00:06,000\n译:Goodbye\n", content)

    def test_provider_failure_does_not_leave_partial_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            file = self.new_file(root, "a.txt", "One\nTwo\n")
            with patch.object(translation, "_translate_one", side_effect=[
                "translated", translation.TranslationError("quota_exceeded", "Quota reached.")
            ]):
                result = worker.handle(self.request(file, remote=True))
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "quota_exceeded")
            self.assertEqual(list(root.glob("a.translated-*")), [])
            self.assertEqual(file.read_text(), "One\nTwo\n")

    def test_deepl_requires_key_and_does_not_expose_secret(self):
        with tempfile.TemporaryDirectory() as directory:
            file = self.new_file(Path(directory), "key.txt", "Hello")
            with patch.dict("os.environ", {"OMNIDROP_DEEPL_API_KEY": ""}):
                response = worker.handle(self.request(file, provider="deepl-free", remote=True))
            self.assertEqual(response["error"]["code"], "missing_api_key")
            request = self.request(file, provider="deepl-free", remote=True)
            request["api_key"] = "secret-123:fx"
            with patch.object(translation, "_post_json", return_value={
                "translations": [{"text": "你好"}]
            }) as mock_post:
                result = worker.handle(request)
            self.assertTrue(result["ok"], result)
            self.assertEqual(mock_post.call_args.args[0], "https://api-free.deepl.com/v2/translate")
            self.assertNotIn("secret-123", json.dumps(result))

    def test_libretranslate_rejects_insecure_public_endpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            file = self.new_file(Path(directory), "a.txt", "Hello")
            request = self.request(file, provider="libretranslate", remote=True)
            request["endpoint"] = "http://external-host.example/translate"
            with patch.object(translation, "_post_json", side_effect=AssertionError("must not call")):
                result = worker.handle(request)
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "invalid_endpoint")
            request["endpoint"] = "http://127.0.0.1:5000/translate"
            with patch.object(translation, "_post_json",
                              return_value={"translatedText": "你好"}) as post:
                result = worker.handle(request)
            self.assertTrue(result["ok"], result)
            self.assertEqual(post.call_args.args[0], "http://127.0.0.1:5000/translate")

    def test_argos_is_offline_without_consent(self):
        with tempfile.TemporaryDirectory() as directory:
            file = self.new_file(Path(directory), "offline.txt", "Hello")
            with patch.object(translation, "_translate_one", return_value="你好"):
                result = worker.handle(self.request(file, provider="argos"))
            self.assertTrue(result["ok"], result)
            self.assertFalse(result["remote"])
            self.assertEqual(result["characters_transmitted"], 0)

    def test_capabilities_are_descriptive_not_false_availability(self):
        result = worker.handle({"command": "capabilities"})
        self.assertTrue(result["ok"])
        self.assertTrue(result["translation"]["requires_explicit_remote_consent"])
        self.assertIn("argos", result["translation"]["providers"])
        self.assertIn(".srt", result["translation"]["extensions"])
        self.assertNotIn("text.translate", result["actions"])


if __name__ == "__main__":
    unittest.main()
