# Roadmap

## Phase 0 - foundation

Completed in the current alpha:

- C++20 / Qt 6 desktop shell.
- Low-noise drag-and-drop UI plus Open File entry point.
- Shared domain and application-service layer.
- CLI using the same action semantics.
- Stable action IDs.
- Python worker protocol v1.
- Async worker execution off the Qt UI thread.
- Runtime capability probing.
- Recent Files, Action Search, keyboard shortcuts, and persistent Activity history.
- Unit/contract-oriented CI baseline.

## Phase 1 - useful daily file operations

### Shared batch workflow

Implemented:

- multi-file Open and drag-and-drop;
- common-action intersection across heterogeneous file selections;
- sequential background batch execution without blocking the UI;
- CLI `batch-actions` and `batch-run` on the same application-service semantics;
- one Activity entry per processed source file;
- visible per-file progress for sequential batches;
- cooperative stop: finish the current file, then do not start the next item.

Next:

- retry failed items;
- persistent queue/job IDs across restart;
- true in-flight cancellation for processors that can safely support it;
- bounded parallelism for processors that are safe to run concurrently.

### Workflow Engine

Implemented in Workflow v1:

- local DAG execution with deterministic dependency ordering and multi-source joins;
- portable, schema-versioned workflow manifests and shared Action ID reuse;
- read-only validation/plan, bounded execution and installed-capability checks;
- per-node result records with output tracking for partial failure recovery;
- Qt recipe builder, workflow file save/load, preview, and background execution;
- CLI workflow validate/plan/run and example manifests;
- explicit denial of arbitrary shell programs and cloud API calls in workflow definitions.

Next:

- draggable DAG graph with editable links, branching, joins and cycle protection: implemented in v2.2;
- persisted visual layout, accessible connection shortcuts and canvas zoom;
- streaming per-operation progress and cooperative stop: implemented in Workflow v2 phase 1;
- persistent local workflow job queue and explicit full-run retry: implemented in v2.3;
- explicit non-recursive folder watching with stable-file detection, output suppression,
  and exclusive cross-process watch locks: implemented in v2.4;
- per-node checkpoint replay and failed-action-only retry;
- finer-grained checkpoint persistence, replay and restart reconciliation;
- typed action parameters and variable propagation;
- consent-aware cloud operations and agent/MCP gateway.

### Images

Implemented:

- JPEG/PNG/WebP compression with non-destructive output;
- PNG/JPEG/BMP/WebP to WebP conversion;
- metadata removal with EXIF orientation preserved visually;
- 90° clockwise/counterclockwise rotation with EXIF orientation normalized;
- non-destructive 50% width/height image resize with Lanczos resampling;
- batch execution for existing image actions.

Next:

- quality/size preview before compression;
- custom-size resize and crop controls;
- OCR;
- animated image semantics.

### PDFs

Implemented:

- text extraction;
- embedded-image extraction with bounded output and safe filenames;
- split into one PDF per page;
- batch execution for existing single-PDF actions;
- merge 2+ selected PDFs into one document while preserving selection order.

Next:

- reorder/rotate;
- image extraction previews and selected-page export;
- OCR scanned documents;
- image-to-PDF and PDF-to-image;
- size optimization with visible quality trade-offs.

### Translation workspace

Implemented in the translation-provider v1 PR:

- text and subtitle translation with offline Argos or explicitly approved online APIs;
- MyMemory, LibreTranslate and DeepL API Free providers;
- shared C++ translation service, GUI dialog and CLI command;
- non-destructive translated copies and privacy-bounded network requests.

Next:

- model-pack installation/verification UX and offline-language discovery;
- document-aware DOCX/PDF translation preserving layout;
- side-by-side original/translation preview;
- translation memory and terminology glossary;
- voice narration of translated documents.

### Text / developer files

Implemented:

- line-ending and trailing-whitespace normalization;
- duplicate-line removal;
- JSON validation + pretty-print formatting;
- XML parsing + pretty-print formatting;
- SHA-256 for any local file.

Next:

- YAML formatting and validation;
- encoding inspection/conversion;
- diff helpers;
- Base64 / URL encoding utilities.

### Archives

Implemented:

- ZIP content inventory without extraction;
- safe ZIP extraction with path-traversal and symbolic-link rejection.

Next:

- 7z/tar/gzip support behind format-aware adapters;
- password-protected archive UX.

### Local voice generation and model packs

Implemented in the optional voice-pack PR:

- versioned voice-pack manifest with SHA-256 model integrity checks;
- explicit standalone model ZIP builder and workflow artifact;
- offline `text.to_speech` through a Piper ONNX voice and a WAV sibling output;
- shared GUI/CLI/agent-ready action identity and batch support;
- zero implicit downloads and no audio/text uploads;
- tests for damaged model packs, subtitles, WAV output and packaging.

Next:

- one-click verified model installation and removal;
- bundled voice-enabled Windows build after runtime redistributability and licensing verification;
- read translated documents aloud / subtitles-to-dubbing pipelines;
- optional lightweight audio/sound-effect models with clear model licenses.

### Media

Next:

- FFmpeg discovery and capability probe;
- video/audio conversion;
- video compression presets;
- audio extraction;
- GIF creation and frame extraction.

## Phase 2 - Windows integration

Implemented:

- Windows executable identity, version metadata, and application icon;
- single-instance activation through a current-user local IPC endpoint;
- second-launch file-open forwarding to the existing window.

Next:

- Explorer context menu;
- Send to OmniDrop;
- richer command palette;
- persistent batch queue;
- URL/deep-link entry points where useful.

Tray/background mode will only be added if a real long-running workflow requires it; OmniDrop
should not stay resident merely for decoration.

## Phase 3 - product reliability

Implemented:

- versioned structured diagnostics snapshot through CLI and GUI clipboard copy;
- diagnostics worker/runtime status without user file paths or file contents.

Next:

- durable settings export/import/reset/recovery;
- structured/rotating logs and diagnostics bundle export;
- richer action history/event timeline;
- job IDs, cancellation, and progress for long-running work;
- crash/startup recovery;
- network timeout/retry/offline semantics for optional online features.

## Phase 4 - extensibility

- plugin SDK;
- versioned local IPC/API;
- agent/MCP gateway built on the same action IDs;
- signed plugin metadata and capability declarations;
- sandboxing/isolation research for third-party processors.

## Phase 5 - distribution

- final application icon and complete Windows identity;
- self-contained CPython 3.13 worker runtime for Windows x64: implemented in v0.3.0 Alpha;
- signed installer/uninstaller;
- portable package;
- verified auto-update with stable/beta channels;
- cryptographic hashes and rollback;
- packaged-build smoke tests.

## Optional AI layer

AI is an enhancement, not the product foundation. Potential explicit actions include document
summarization, table extraction, semantic rename, and local-model OCR/document understanding.
Providers must be user-selectable, and local/core operations must remain useful without AI.
