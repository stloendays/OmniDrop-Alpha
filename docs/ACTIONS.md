# Action contracts

Action IDs are public automation contracts. Labels and descriptions may evolve, but an ID must
not be silently renamed once published. Prefer adding a new ID and preserving a compatibility
alias when semantics genuinely change.

## Current registry

| ID | File kind | State | Backend | Output semantics |
| --- | --- | --- | --- | --- |
| `file.sha256` | any | implemented | Python stdlib | no file output; returns digest |
| `file.find_duplicates` | 2-128 selected files | implemented, batch-only | Python stdlib | new JSON report, no files deleted or changed |
| `text.normalize` | text/developer | implemented | Python stdlib | new sibling file |
| `text.deduplicate` | text/developer | implemented | Python stdlib | new sibling file |
| `text.format_json` | JSON | implemented | Python stdlib | validated, pretty-printed sibling `.json` file |
| `text.format_xml` | XML | implemented | Python stdlib | parsed, indented sibling `.xml` file |
| `text.to_speech` | TXT/MD/SRT/VTT | implemented, runtime-gated | optional Piper + verified voice pack | local-only unique `.spoken.wav` sibling |
| `image.compress` | image | implemented | Pillow | new sibling file |
| `image.convert_webp` | image | implemented | Pillow | new `.webp` sibling file |
| `image.rotate_clockwise` | PNG/JPEG/WebP/BMP/TIFF | implemented | Pillow | new sibling file rotated 90° clockwise |
| `image.rotate_counterclockwise` | PNG/JPEG/WebP/BMP/TIFF | implemented | Pillow | new sibling file rotated 90° counterclockwise |
| `image.resize_half` | PNG/JPEG/WebP/BMP/TIFF | implemented | Pillow | new `.half` sibling file; width/height halved (rounded up); EXIF orientation normalized |
| `image.remove_metadata` | image | implemented | Pillow | new sibling file |
| `image.ocr` | image | planned | OCR adapter | TBD |
| `pdf.compress` | PDF | planned | PDF adapter | TBD |
| `pdf.extract_text` | PDF | implemented | pypdf | new `.txt` sibling file |
| `pdf.split` | PDF | implemented | pypdf | new output directory |
| `pdf.rotate_clockwise` | PDF | implemented | pypdf | new sibling PDF with every page rotated 90° clockwise |
| `pdf.rotate_counterclockwise` | PDF | implemented | pypdf | new sibling PDF with every page rotated 90° counterclockwise |
| `pdf.merge` | 2+ PDFs | implemented | pypdf | one merged sibling PDF; selected input order is preserved |
| `pdf.extract_images` | PDF | implemented, runtime-gated | pypdf + Pillow | new sibling folder; embedded images extracted with generated safe page/image filenames |
| `video.compress` | video | planned | FFmpeg | TBD |
| `video.extract_audio` | video | planned | FFmpeg | TBD |
| `video.convert` | video | planned | FFmpeg | TBD |
| `audio.convert` | audio | planned | FFmpeg | TBD |
| `audio.compress` | audio | planned | FFmpeg | TBD |
| `archive.inspect` | ZIP | implemented | Python stdlib | new inventory `.txt` file |
| `archive.extract` | ZIP | implemented | Python stdlib | new output directory |

### Image resizing semantics

`image.resize_half` uses high-quality Lanczos resampling and creates a separate file, never
overwriting the source. Dimensions are computed after normalizing EXIF orientation, then each
dimension is halved with rounding up and a minimum of one pixel. Where supported, ICC color
profile and non-orientation EXIF tags are copied; orientation is embedded in the pixel order.
The worker returns `original_width`, `original_height`, `output_width`, and `output_height`
in addition to the usual `output_path`. Multi-frame/animated inputs are rejected rather than
silently flattening frames. The preset is fixed; the worker protocol remains v1.

## Action scope

Actions have an explicit execution scope:

- **per-file** actions run independently for each selected source file;
- **batch** actions consume the ordered selection as one operation and produce one combined result.

`pdf.merge` is a batch action. It requires at least two PDF files and preserves the selection order
when appending pages. The input PDFs are not modified.

## PDF rotation semantics

PDF rotation actions use page-level 90-degree rotation metadata through pypdf, apply the same
direction to every page, preserve the source PDF, and write a new sibling PDF. Clockwise rotation
adds 90 degrees; counterclockwise rotation adds 270 degrees modulo 360.

## Image rotation semantics

Rotation actions normalize EXIF orientation before rotating pixels, preserve the source file,
and preserve common color-profile/EXIF metadata where the destination format supports it.
Animated images are intentionally unavailable for rotation until frame-preserving semantics
are implemented.

## Runtime availability

`implemented` is not the same as `available`.

The catalog declares which actions have an implementation. Runtime adapters then report which
implemented actions are reachable on the current machine. The GUI and CLI intersect those two
sets. This keeps a missing Pillow, pypdf, FFmpeg, OCR runtime, or future plugin from being shown
as runnable.

## Destructive behavior

The default contract for file transforms is non-destructive: produce a new sibling file or a new
output directory. Any future in-place mutation must use a distinct action ID and make the destructive
semantics explicit before execution.
