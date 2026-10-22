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
