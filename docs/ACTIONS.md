# Action contracts

Action IDs are public automation contracts. Labels and descriptions may evolve, but an ID must
not be silently renamed once published. Prefer adding a new ID and preserving a compatibility
alias when semantics genuinely change.

## Current registry

| ID | File kind | State | Backend | Output semantics |
| --- | --- | --- | --- | --- |
| `file.sha256` | any | implemented | Python stdlib | no file output; returns digest |
| `text.normalize` | text/developer | implemented | Python stdlib | new sibling file |
| `text.deduplicate` | text/developer | implemented | Python stdlib | new sibling file |
| `image.compress` | image | implemented | Pillow | new sibling file |
| `image.convert_webp` | image | implemented | Pillow | new `.webp` sibling file |
| `image.remove_metadata` | image | implemented | Pillow | new sibling file |
| `image.ocr` | image | planned | OCR adapter | TBD |
| `pdf.compress` | PDF | planned | PDF adapter | TBD |
| `pdf.extract_text` | PDF | implemented | pypdf | new `.txt` sibling file |
| `pdf.split` | PDF | implemented | pypdf | new output directory |
| `pdf.extract_images` | PDF | planned | PDF adapter | TBD |
| `video.compress` | video | planned | FFmpeg | TBD |
| `video.extract_audio` | video | planned | FFmpeg | TBD |
| `video.convert` | video | planned | FFmpeg | TBD |
| `audio.convert` | audio | planned | FFmpeg | TBD |
| `audio.compress` | audio | planned | FFmpeg | TBD |
| `archive.inspect` | ZIP | implemented | Python stdlib | new inventory `.txt` file |
| `archive.extract` | ZIP | implemented | Python stdlib | new output directory |

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
