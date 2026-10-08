# OmniDrop

**One local app for files.**

OmniDrop is an open-source, local-first Windows utility for inspecting, converting,
compressing, extracting, cleaning, and transforming files from one drag-and-drop surface.

The product principle is simple:

> Drop a file. See only the actions that make sense for that file.

No account is required. Core file operations are designed to run locally. Network or AI
features, when added, must be explicit opt-in actions rather than hidden behavior.

## Why OmniDrop is different

OmniDrop is not trying to duplicate a complete office suite. Its focus is
**drop -> context-aware actions -> optional private/local processors -> reproducible output**.
C++/Qt, CLI and automation share the same action IDs; users can keep their original files
and choose when to use third-party providers.

New optional workspaces:

- [Translation](docs/TRANSLATION.md) - local Argos or explicitly approved MyMemory,
  LibreTranslate and DeepL API Free providers for text/Markdown/subtitles.
- [Offline voice packs](docs/VOICE_PACKS.md) - attach a verified small ONNX speech model and
  convert text/subtitles to WAV without uploading the source. The model pack is a separate
  artifact; an optional Piper runtime is required.
- [Workflow Engine](docs/WORKFLOWS.md) - link existing local file actions into reusable,
  versioned pipelines. Includes Qt recipe builder, CLI preflight/execution, DAG branching,
  dependency-aware file passing, and non-destructive output tracking.

## Status

OmniDrop is in early alpha (`0.1.0`). The current vertical slice includes:

- Qt 6 / C++20 desktop shell with drag-and-drop and an Open File path.
- File-kind detection and context-aware action recommendations.
- Searchable action list (`Ctrl+F`).
- Persistent Recent Files and Activity history using local Qt settings.
- Runtime worker capability probing: missing processors show as unavailable instead of pretending to work.
- One shared application-service layer used by GUI and CLI.
- Stable string action IDs such as `file.sha256` and `pdf.extract_text`.
- A stateless JSON protocol for Python workers.
- Async execution so worker operations do not block the Qt UI thread.
- Windows and Linux-core CI definitions.

### Working actions

| File kind | Action | Action ID | Backend |
| --- | --- | --- | --- |
| Any file | SHA-256 | `file.sha256` | Python stdlib |
| Text / code | Normalize text | `text.normalize` | Python stdlib |
| Text / code | Remove duplicate lines | `text.deduplicate` | Python stdlib |
| Text / subtitles | Generate speech WAV (optional) | `text.to_speech` | Verified Piper voice pack |
| Image | Compress image | `image.compress` | Pillow |
| Image | Convert to WebP | `image.convert_webp` | Pillow |
| Image | Remove metadata | `image.remove_metadata` | Pillow |
| PDF | Extract text | `pdf.extract_text` | pypdf |
| PDF | Split into one file per page | `pdf.split` | pypdf |
| ZIP | Inspect contents | `archive.inspect` | Python stdlib |
| ZIP | Safe extract | `archive.extract` | Python stdlib |

All transform actions create a new output path; they do not overwrite the source file.
Actions without an implemented backend remain visible as `planned`. Implemented actions whose
local dependency is missing appear as `unavailable`.

## Design direction

The desktop UI uses a low-noise black/white/gray system. The main surface is a drop target,
not a dashboard full of tool categories.

```text
Qt GUI / CLI / future shell extension / future agent API
                       |
              application services
                       |
             normalized action model
                /             \
        native adapters     worker adapters
                              |
                   Python / FFmpeg / OCR / PDF
```

Business behavior must not live only inside Qt slots. Any meaningful capability should be
reachable through the same application service from GUI and automation surfaces.

## Build

Requirements:

- CMake 3.24+
- C++20 compiler
- Qt 6.5+ (`Core`, `Widgets`, `Concurrent`)
- Python 3.10+

Install worker dependencies:

```powershell
python -m pip install -r python/requirements.txt
```

Configure, build, and test:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -m unittest discover -s tests -p "test_*.py"
```

Run the GUI from the build output and drop a local file into the window.

For Python worker discovery, OmniDrop checks `OMNIDROP_WORKER` first and then common
relative development paths. `OMNIDROP_PYTHON` can override the Python executable.

## CLI

```text
omnidrop-cli inspect <file>
omnidrop-cli actions <file>
omnidrop-cli capabilities
omnidrop-cli run <action-id> <file>
omnidrop-cli batch-run <action-id> <file> <file> [...]
omnidrop-cli translate <file> --from en --to zh --provider argos|mymemory|libretranslate|deepl-free [--allow-upload]
omnidrop-cli worker-ping
```

The CLI exposes the same normalized action IDs used by the GUI. `actions` performs a runtime
capability probe so unavailable local processors are reported accurately.

## Repository map

```text
src/domain/        stable file/action domain model
src/app/           action catalog and application services
src/adapters/      worker, persistence, and future platform adapters
src/gui/           Qt desktop presentation
src/cli/           automation-friendly CLI
python/            stateless local worker and dependencies
tests/             C++ catalog tests and Python worker contract tests
docs/              architecture, action contracts, and roadmap
```

## Roadmap

Near-term work is focused on a small number of high-value local operations:

1. Image resize/crop/rotate and OCR.
2. PDF merge/reorder/rotate, extract images, OCR, image-to-PDF and optimization.
3. More archive formats plus FFmpeg-backed video/audio compress/convert/extract-audio.
4. Batch operations and a richer command palette.
5. Explorer context menu and `Send to OmniDrop` integration.
6. Plugin SDK and stable automation/agent interface.
7. Optional AI providers and local-model adapters, always explicit and optional.
8. Self-contained Windows runtime, signed installer, verified auto-update, rollback, and release channels.

See [docs/ROADMAP.md](docs/ROADMAP.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and
[docs/ACTIONS.md](docs/ACTIONS.md).

## Principles

- Local-first.
- No account required for core operations.
- No hidden uploads or telemetry.
- Fast native desktop shell.
- Source files are not overwritten by transform actions.
- Additive, stable action IDs and callable interfaces.
- GUI, CLI, and future integrations share one semantic model.
- Strong status color is reserved for real attention states; helper text stays secondary.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). For security-sensitive reports, see [SECURITY.md](SECURITY.md).

## License

MIT. See [LICENSE](LICENSE).
