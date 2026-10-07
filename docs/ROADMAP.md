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

### Images

Implemented:

- JPEG/PNG/WebP compression with non-destructive output;
- PNG/JPEG/BMP/WebP to WebP conversion;
- metadata removal with EXIF orientation preserved visually;
- batch execution for existing image actions.

Next:

- quality/size preview before compression;
- resize/crop/rotate;
- OCR;
- animated image semantics.

### PDFs

Implemented:

- text extraction;
- split into one PDF per page;
- batch execution for existing single-PDF actions;
- merge 2+ selected PDFs into one document while preserving selection order.

Next:

- reorder/rotate;
- extract embedded images;
- OCR scanned documents;
- image-to-PDF and PDF-to-image;
- size optimization with visible quality trade-offs.

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

- durable settings export/import/reset/recovery;
- diagnostics bundle and structured logs;
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
- self-contained worker runtime so ordinary users do not need to install Python;
- signed installer/uninstaller;
- portable package;
- verified auto-update with stable/beta channels;
- cryptographic hashes and rollback;
- packaged-build smoke tests.

## Optional AI layer

AI is an enhancement, not the product foundation. Potential explicit actions include document
summarization, table extraction, semantic rename, and local-model OCR/document understanding.
Providers must be user-selectable, and local/core operations must remain useful without AI.
