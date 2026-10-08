# OmniDrop Voice Packs

**Local text-to-speech without a subscription or an uploaded document.**

OmniDrop's `text.to_speech` action converts a selected TXT/Markdown/SRT/VTT file to a new
WAV using the optional Piper ONNX inference runtime and an installed, integrity-checked voice pack.
The same action works in the GUI, CLI, and sequential batch processing.

## Why an optional voice pack?

Neural weights are substantial compared with a lightweight file utility. The first supported
English model (`en_US-ljspeech-medium`) is about 63.5 MB. Bundling it in every installer
would make the core app unnecessarily heavy. OmniDrop ships the **voice pack as a separate,
downloadable ZIP**; extracting it into the app folder extends the portable package without
altering the EXE or requiring cloud processing.

The model is based on the public-domain LJ Speech dataset and was trained from scratch,
according to its upstream model card. Source: [Piper voices on Hugging Face](https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_US/ljspeech/medium).
Model downloads are pinned to the published SHA-256
`6f52a751e2349abe7a76735eb09dc1875298c77ea2342ffd2fef79ff81b87f22`.
Always retain the pack's `MODEL_CARD` and `NOTICE.txt` when redistributing.

## Install the optional voice model

1. Download `OmniDrop-VoicePack-en_US-ljspeech-medium.zip` from the
   [voice-pack workflow](https://github.com/stloendays/OmniDrop-Alpha/actions/workflows/voice-pack.yml)
   after it finishes successfully, or run the builder yourself.
2. Check its companion `.sha256`.
3. Unzip into the folder containing `OmniDrop.exe`. The ZIP installs
   `models/voice-packs/en_US-ljspeech-medium/` with the ONNX model, config,
   `manifest.json`, `MODEL_CARD`, and `NOTICE.txt`.
4. Install a compatible **optional** Piper runtime in the Python environment
   used by OmniDrop, for example `python -m pip install -r python/requirements-voice.txt`.
   Piper runtime versions may have GPLv3 obligations if redistributed;
   this model-only ZIP does **not** bundle or relicense the inference runtime.

For development, generate the ZIP with an explicit fetch:

```powershell
python scripts/build_voice_pack.py --fetch
```

For an alternate voice-pack location, set `OMNIDROP_VOICE_PACK_DIR`. For a
specific installed voice, set `OMNIDROP_VOICE_ID`. No model or dependency is
downloaded automatically when OmniDrop starts or when its capabilities are probed.

## Use it

Open a plain-text document or subtitle in OmniDrop, then select **Create spoken WAV**.
This action is enabled only when both a verified voice pack and the Piper runtime
are available. No file content leaves your computer.

```powershell
omnidrop-cli capabilities
omnidrop-cli actions chapter.txt
omnidrop-cli run text.to_speech chapter.txt
omnidrop-cli batch-run text.to_speech first.txt second.txt
```

Outputs are unique `*.spoken.wav` siblings of the original files. Inputs remain untouched.
The worker validates its model SHA-256 and config before using it. A corrupted model
is ignored rather than silently executed.

## Known limits and next steps

- This is **text-to-speech**, not an unrestricted music or sound-effects generative model.
- The current voice pack is English-only and uses a fixed default voice. Further
  voices/languages are separate compatible packs.
- Text extraction for Markdown and subtitle captions is conservative. It is not
  a full semantic document reader.
- Per-file narration is limited to 8,000 spoken characters and 100 KB inputs.
- Piper and Argos are separately installed optional inference dependencies.
- Future work: a GUI model manager with hashes, language selection, speech playback,
  text/translation/subtitle-to-voice pipelines, speech segmentation, audio editing,
  and a smaller offline sound-effects model where licensing, safety and CPU performance
  are suitable.

Do not represent these optional model capabilities as included in the core
`OmniDrop-windows-dev.zip` until a voice-enabled package passes its own smoke tests.
