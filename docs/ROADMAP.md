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

### Images

Implemented:

- JPEG/PNG/WebP compression with non-destructive output;
- PNG/JPEG/BMP/WebP to WebP conversion;
- metadata removal with EXIF orientation preserved visually.

Next:

- quality/size preview before compression;
- resize/crop/rotate;
- OCR;
- multi-file batch mode;
- animated image semantics.

### PDFs

Implemented:

- text extraction;
- split into one PDF per page.

Next:

- merge/reorder/rotate;
- extract embedded images;
- OCR scanned documents;
- image-to-PDF and PDF-to-image;
- size optimization with visible quality trade-offs.

### Text / developer files

Implemented:

- line-ending and trailing-whitespace normalization;
- duplicate-line removal;
- SHA-256 for any local file.

Next:

- JSON/XML/YAML formatting and validation;
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

- Explorer context menu;
- Send to OmniDrop;
- richer command palette;
- batch queue;
- single-instance activation;
- file/deep-link entry points where useful.

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
