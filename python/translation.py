"""Explicit, provider-neutral translation of local UTF-8 text files.

Network transmission is opt-in per operation. No file paths, keys or raw
document text are included in worker responses or exception messages.
"""

from __future__ import annotations

import html
import json
import os
import re
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

SUPPORTED_SUFFIXES = {".txt", ".md", ".markdown", ".srt", ".vtt"}
REMOTE_PROVIDERS = {"mymemory", "libretranslate", "deepl-free"}
ALL_PROVIDERS = REMOTE_PROVIDERS | {"argos"}
LANGUAGE_PATTERN = re.compile(r"^[a-z]{2}(?:-[a-z]{2})?$")
MAX_FILE_BYTES = 100_000
MAX_SEGMENTS = 250
MAX_REMOTE_CHARACTERS = 40_000
USER_AGENT = "OmniDrop/0.1 (user-initiated translation)"


class TranslationError(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def is_remote(provider: str) -> bool:
    return provider in REMOTE_PROVIDERS


def validate_endpoint(value: str) -> str:
    parsed = urllib.parse.urlsplit(value.strip())
    host = (parsed.hostname or "").lower()
    local = host in {"localhost", "127.0.0.1", "::1"}
    if (parsed.scheme != "https" and not (parsed.scheme == "http" and local)):
        raise TranslationError(
            "invalid_endpoint", "Remote translation endpoints must use HTTPS; HTTP is allowed for local loopback only."
        )
    if not host or parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise TranslationError("invalid_endpoint", "Provide a plain translation endpoint without URL credentials or parameters.")
    if not parsed.path.endswith("/translate"):
        raise TranslationError("invalid_endpoint", "LibreTranslate endpoint must end with /translate.")
    return value.strip()


def _post_json(url: str, payload: dict[str, Any], headers: dict[str, str]) -> dict[str, Any]:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url, data=body, method="POST",
        headers={"Content-Type": "application/json", "User-Agent": USER_AGENT, **headers},
    )
    return _fetch_json(request)


def _fetch_json(request: urllib.request.Request) -> dict[str, Any]:
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            body = response.read(1024 * 1024)
        payload = json.loads(body)
        if not isinstance(payload, dict):
            raise ValueError("Non-object translation response.")
        return payload
    except urllib.error.HTTPError as exc:
        # Never echo response bodies, requested text, the query URL or API credentials.
        if exc.code in {429, 456}:
            raise TranslationError("quota_exceeded", "Translation provider rate or usage limit reached.") from None
        if exc.code in {401, 403}:
            raise TranslationError("unauthorized", "Translation provider rejected authentication.") from None
        raise TranslationError("provider_http_error", f"Translation provider returned HTTP {exc.code}.") from None
    except (urllib.error.URLError, TimeoutError) as exc:
        raise TranslationError("network_error", "Translation provider is unreachable or timed out.") from None
    except (json.JSONDecodeError, UnicodeError, ValueError) as exc:
        raise TranslationError("provider_response_error", "Translation provider returned an invalid response.") from None


def _translate_one(text: str, provider: str, source: str, target: str,
                   endpoint: str, api_key: str) -> str:
    if provider == "argos":
        try:
            from argostranslate.translate import translate
            return translate(text, source, target)
        except ImportError:
            raise TranslationError(
                "missing_dependency", "Install argostranslate and the selected language model for offline translation."
            ) from None
        except Exception:
            raise TranslationError(
                "model_unavailable", "Argos language pair is unavailable; install the corresponding offline model."
            ) from None

    if provider == "mymemory":
        if source == "auto":
            raise TranslationError("invalid_language", "MyMemory requires an explicit source language.")
        query: dict[str, str] = {"q": text, "langpair": f"{source}|{target}"}
        email = os.getenv("OMNIDROP_MYMEMORY_EMAIL", "").strip()
        if email:
            query["de"] = email
        url = "https://api.mymemory.translated.net/get?" + urllib.parse.urlencode(query)
        request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        data = _fetch_json(request)
        if data.get("responseStatus") != 200:
            raise TranslationError("provider_rejected", "MyMemory could not translate this segment (possibly quota exceeded).")
        translation = data.get("responseData", {}).get("translatedText")
        if not isinstance(translation, str):
            raise TranslationError("provider_response_error", "MyMemory response is missing translated text.")
        return html.unescape(translation)

    if provider == "libretranslate":
        url = validate_endpoint(endpoint or os.getenv("OMNIDROP_LIBRETRANSLATE_URL", "http://127.0.0.1:5000/translate"))
        payload = {"q": text, "source": source, "target": target, "format": "text"}
        secret = api_key or os.getenv("OMNIDROP_LIBRETRANSLATE_API_KEY", "")
        if secret:
            payload["api_key"] = secret
        data = _post_json(url, payload, {})
        result = data.get("translatedText")
    elif provider == "deepl-free":
        secret = api_key or os.getenv("OMNIDROP_DEEPL_API_KEY", "")
        if not secret:
            raise TranslationError("missing_api_key", "Set OMNIDROP_DEEPL_API_KEY or enter an API key in the desktop dialog.")
        payload = {"text": [text], "target_lang": target.upper()}
        if source != "auto":
            payload["source_lang"] = source.upper()
        data = _post_json(
            "https://api-free.deepl.com/v2/translate",
            payload,
            {"Authorization": "DeepL-Auth-Key " + secret},
        )
        translations = data.get("translations")
        result = translations[0].get("text") if isinstance(translations, list) and translations and isinstance(translations[0], dict) else None
    else:
        raise TranslationError("invalid_provider", "Unsupported translation provider.")

    if not isinstance(result, str):
        raise TranslationError("provider_response_error", "Translation provider did not return translated text.")
    return result


def _segments(text: str, max_bytes: int) -> list[str]:
    """UTF-8-safe segmentation, preferring whitespace over splitting words."""
    chunks: list[str] = []
    remaining = text.strip()
    while remaining:
        if len(remaining.encode("utf-8")) <= max_bytes:
            chunks.append(remaining)
            break

        current_bytes = 0
        boundary = 0
        last_space = 0
        for index, char in enumerate(remaining):
            encoded = len(char.encode("utf-8"))
            if current_bytes + encoded > max_bytes:
                break
            current_bytes += encoded
            boundary = index + 1
            if char.isspace() or char in ".!?。！？;；":
                last_space = index + 1

        if boundary == 0:
            raise TranslationError("segment_limit", "A character exceeded the provider's size limit.")

        cut = last_space if last_space > boundary // 2 else boundary
        chunk = remaining[:cut].strip()
        if chunk:
            chunks.append(chunk)
        remaining = remaining[cut:].lstrip()

    return chunks


def _should_skip(line: str, suffix: str, in_code: bool) -> bool:
    trimmed = line.strip()
    if not trimmed:
        return True
    if suffix in {".srt", ".vtt"}:
        if trimmed.isdecimal() or "-->" in trimmed or trimmed == "WEBVTT":
            return True
    if suffix in {".md", ".markdown"}:
        if in_code or "`" in trimmed or trimmed.startswith(("<!--", "![", "|", "---")):
            return True
        if trimmed.startswith(("http://", "https://")):
            return True
    return False


def translate_file(path: Path, source_lang: str, target_lang: str, provider: str,
                   allow_remote: bool = False, endpoint: str = "", api_key: str = "") -> dict[str, Any]:
    if provider not in ALL_PROVIDERS:
        raise TranslationError("invalid_provider", "Choose argos, mymemory, libretranslate or deepl-free.")
    source, target = source_lang.strip().lower(), target_lang.strip().lower()
    if not LANGUAGE_PATTERN.fullmatch(target):
        raise TranslationError("invalid_language", "Choose a valid target language code.")
    if source != "auto" and not LANGUAGE_PATTERN.fullmatch(source):
        raise TranslationError("invalid_language", "Choose a valid source language code.")
    if source == target:
        raise TranslationError("invalid_language", "Source and target languages must differ.")
    if source == "auto" and provider in {"mymemory", "argos"}:
        raise TranslationError("invalid_language", "This provider requires an explicit source language.")
    if is_remote(provider) and not allow_remote:
        raise TranslationError("consent_required", "Confirm third-party transmission before enabling online translation.")
    if provider == "libretranslate":
        validate_endpoint(endpoint or os.getenv("OMNIDROP_LIBRETRANSLATE_URL", "http://127.0.0.1:5000/translate"))

    if path.suffix.lower() not in SUPPORTED_SUFFIXES:
        raise TranslationError("unsupported_format", "Translation supports UTF-8 TXT, Markdown, SRT and VTT files in this version.")
    if not path.is_file():
        raise TranslationError("not_found", "Input file does not exist.")
    if path.stat().st_size > MAX_FILE_BYTES:
        raise TranslationError("file_too_large", "Input is over the 100 KB first-version safety limit.")
    try:
        contents = path.read_text(encoding="utf-8-sig")
    except UnicodeError:
        raise TranslationError("invalid_encoding", "Input must be UTF-8 text.") from None

    suffix = path.suffix.lower()
    lines = contents.splitlines(keepends=True)
    in_code = False
    collected: list[str] = []
    total_segments = 0
    transmitted = 0
    # MyMemory explicitly limits GET?q to 500 UTF-8 bytes.
    max_segment_bytes = 450 if provider == "mymemory" else 1800
    estimated_chars = len(contents)
    if is_remote(provider) and estimated_chars > MAX_REMOTE_CHARACTERS:
        raise TranslationError("file_too_large", "Online translation is limited to 40,000 input characters per operation.")
    if provider == "mymemory" and estimated_chars > (50_000 if os.getenv("OMNIDROP_MYMEMORY_EMAIL") else 5_000):
        raise TranslationError("quota_guard", "This file exceeds MyMemory's documented daily free allowance.")

    for line in lines:
        without_newline = line.rstrip("\r\n")
        newline = line[len(without_newline):]
        trimmed = without_newline.strip()
        if suffix in {".md", ".markdown"} and trimmed.startswith(("```", "~~~")):
            in_code = not in_code
            collected.append(line)
            continue
        if _should_skip(without_newline, suffix, in_code):
            collected.append(line)
            continue

        prefix = re.match(r"^[ \t]*(?:(?:#{1,6}[ \t]+|>[ \t]+|[-*+][ \t]+|\d+[.)][ \t]+))?", without_newline)
        markup_prefix = prefix.group(0) if prefix else ""
        remainder = without_newline[len(markup_prefix):]
        if not remainder.strip():
            collected.append(line)
            continue

        tokens = _segments(remainder, max_segment_bytes)
        total_segments += len(tokens)
        if total_segments > MAX_SEGMENTS:
            raise TranslationError("segment_limit", "Input would require too many translation requests.")
        translations = []
        for token in tokens:
            output = _translate_one(token, provider, source, target, endpoint, api_key)
            translations.append(output.strip())
            if is_remote(provider):
                transmitted += len(token)
        joined = " ".join(translations)
        trailing = len(remainder) - len(remainder.rstrip(" \t"))
        collected.append(markup_prefix + joined + (" " * trailing) + newline)

    if not lines and not contents:
        raise TranslationError("empty_input", "Input document is empty.")

    marker = f"translated-{target.replace('-', '_')}"
    candidate = path.with_name(f"{path.stem}.{marker}{path.suffix}")
    counter = 2
    while candidate.exists():
        candidate = path.with_name(f"{path.stem}.{marker}-{counter}{path.suffix}")
        counter += 1

    # Output is created only after the entire operation succeeds.
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    fd = os.open(candidate, flags, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as stream:
            stream.write("".join(collected))
    except Exception:
        candidate.unlink(missing_ok=True)
        raise

    return {
        "provider": provider,
        "remote": is_remote(provider),
        "source_lang": source,
        "target_lang": target,
        "input_path": str(path),
        "output_path": str(candidate),
        "segments_translated": total_segments,
        "characters_transmitted": transmitted,
    }
