# MinerU scientific PDF parsing (optional)

OmniDrop can use [MinerU 4](https://github.com/opendatalab/MinerU) for multi-column scientific articles, equations, tables, figure captions, and page-wise layout data. This is **not** an OCR or extraction-accuracy guarantee; verify all scientific measurements against the PDF and Supporting Information.

## Setup

Install MinerU 4 in a separate environment as documented upstream. Confirm the command `mineru-kit` is accessible, or set `OMNIDROP_MINERU_CLI` to its executable path. The minimal OmniDrop Python runtime does **not** bundle MinerU or its large model weights.

At runtime the app probes for the executable. Without it the three actions appear as **unavailable** rather than failing as implemented actions. The first run may need to download model files; for strictly offline operation, pre-cache the required models following MinerU's documentation.

## Actions

| ID | Output | Notes |
| --- | --- | --- |
| `pdf.parse_scientific` | Markdown (.md) | May produce linked assets in the output directory |
| `pdf.parse_scientific_json` | MiddleJson (.json) | Requires `schema=docvortex.middle`; not the same as ordinary extracted values |
| `pdf.export_scientific_bundle` | ZIP (.zip) | Full package preserving figures and structured outputs |

Outputs are placed in **new sibling directories**, with unique paths and untouched source PDFs. Failed/partial results are removed. Run-time timeout defaults to 1800 s, configurable with `OMNIDROP_MINERU_TIMEOUT_SECONDS` (60–1800 s). OmniDrop does not pass `MINERU_API_URL` or `MINERU_API_KEY` to this adapter.

### Built-in result inspection

Every successful parse additionally produces:

- `preview.md` — bounded read-only excerpt (maximum 48,000 content characters). It is not a complete export and shows formulas as LaTeX source, not typeset equations.
- `provenance.json` — source SHA-256 and file size, requested tier, output format, UTC processing time, optional page count and block-type inventory, and `validation_status=not_manually_verified`.
- For ZIP exports, up to **8** small image previews in `preview_images/`. ZIP paths are never used directly as extraction paths; figures are given generated filenames, with per-image size and pixel checks.

The Qt GUI opens a read-only scientific preview for successful single-PDF actions. The text is rendered as **inert plain text**, not untrusted HTML. No browser runtime or outbound image retrieval is used. Open the original PDF or the produced ZIP for full-size figures and validation. The CLI returns additive `preview_path`, `provenance_path`, `preview_images` and `page_count` keys in its worker JSON response.

MinerU 4's `mineru-kit parse` processes **all PDF pages by default**. The document-library `mineru parse` command is different and defaults to its first-ten-page window. MiddleJson uses `schema=docvortex.middle`, `schema_version=2.0` and a `pages` array. Standalone JSON intentionally omits some image bytes; use ZIP when source image assets are essential.

The worker protocol remains v1. The C++ GUI and CLI use the same action IDs:

```powershell
omnidrop-cli capabilities
omnidrop-cli actions paper.pdf
omnidrop-cli run pdf.parse_scientific paper.pdf
omnidrop-cli run pdf.parse_scientific_json paper.pdf
omnidrop-cli run pdf.export_scientific_bundle paper.pdf
```

## Research data provenance

MinerU extracts *candidate* textual/structural content, not validated scientific measurements. For a tabulated value or digitized figure, keep original PDF name, DOI, page, table/figure/SI locator, printed/extracted/derived type, original unit, conversion, and manual verification status. Never treat a parsed JSON number as independently verified.

Reference: [MinerU output schema documentation](https://opendatalab.github.io/MinerU/reference/output_files/).
