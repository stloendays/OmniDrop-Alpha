# OmniDrop

<p align="center">
  <strong>Drop a file. Find the right action. Make the workflow your own.</strong>
</p>

<p align="center">
  A local-first desktop workspace for everyday file tools and visual automation.<br>
  Built with C++20, Qt 6 and a Python worker. No account required for core tasks.
</p>

<p align="center">
  <a href="https://github.com/stloendays/OmniDrop-Alpha/releases/tag/v0.2.0"><strong>Download Windows Alpha</strong></a>
  · <a href="#quick-start">Quick start</a>
  · <a href="#what-can-it-do">Features</a>
  · <a href="#workflows">Workflows</a>
  · <a href="docs/README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <a href="https://github.com/stloendays/OmniDrop-Alpha/actions/workflows/build.yml">Build &amp; tests</a>
  · <a href="https://github.com/stloendays/OmniDrop-Alpha/releases">Releases</a>
  · <a href="LICENSE">MIT License</a>
  · <a href="CONTRIBUTING.md">Contributing</a>
</p>

<!-- VISUAL SLOT 01 / HERO: assets/readme/hero.webp
     Reserved for a real, verified OmniDrop screenshot or approved illustration.
     No placeholder artwork or broken image link is published yet. -->

> [!IMPORTANT]
> **Early Alpha · v0.2.0.** The current downloadable package is an **unsigned Windows x64 portable ZIP**, not a production-ready installer. It contains Qt and bundled worker libraries but **requires an installed Python 3.10+ interpreter**. See [release notes](docs/releases/v0.2.0.md) before testing.

## Why OmniDrop?

Most file tasks should not require opening five different tools, uploading a document, or writing a throwaway script.

OmniDrop brings **context-aware file actions**, **non-destructive outputs**, and **reusable workflows** into one focused workspace. Drop a file and see actions relevant to its format *and* to the processors actually available on your computer. Use one action, process a batch, or connect them into a visual workflow.

**One drop → relevant tools → new outputs → optional automation.**

Unlike a general-purpose office suite, OmniDrop concentrates on the file operations *around* your documents. The goal is a fast, understandable utility—not a claim that Office editing, OCR or every media format is already supported.

## What can it do?

| Area | Available today |
| --- | --- |
| **Text & developer files** | Normalize whitespace/line endings, remove duplicate lines, format JSON/XML, calculate SHA-256 |
| **Images** | Compress, convert to WebP, remove metadata, rotate 90° and resize to 50% |
| **PDFs** | Extract text, split pages, merge PDFs in selected order and rotate pages |
| **ZIP archives** | Inspect archive contents and safely extract files |
| **Batch operations** | Process multiple selected files with per-file progress and cooperative stop |
| **Workflow Builder** | Create, edit, preview, save and run DAGs with branches and joins |
| **Workflow Jobs** | Save a local queue, inspect status and explicitly retry stopped/failed/interrupted jobs |
| **Folder Watch** | Opt in to one folder, process stable new files and exclude generated outputs |
| **Translation (optional)** | Offline Argos or explicitly consented MyMemory / LibreTranslate / DeepL API Free |
| **Speech (optional)** | Convert text/subtitles to WAV after installing a verified Piper voice pack and inference runtime |

**Reality-based availability:** OmniDrop distinguishes *planned*, *implemented but unavailable*, and *available here* actions. Optional processors, language packs and voice models are not installed silently.

[Full Action IDs and status](docs/ACTIONS.md) · [Translation and privacy](docs/TRANSLATION.md) · [Offline voice packs](docs/VOICE_PACKS.md)

<!-- VISUAL SLOT 02 / FILE ACTIONS: assets/readme/workspace.webp
     Real product capture to be added later. -->

## Quick start

### Download for Windows

1. Open **[OmniDrop v0.2.0 Alpha](https://github.com/stloendays/OmniDrop-Alpha/releases/tag/v0.2.0)** and get \`OmniDrop-0.2.0-windows-dev.zip\` **plus** its \`.sha256\` file.
2. Compare the archive's SHA-256 against the companion checksum. In PowerShell:

   \`\`\`powershell
   Get-FileHash .\OmniDrop-0.2.0-windows-dev.zip -Algorithm SHA256
   Get-Content .\OmniDrop-0.2.0-windows-dev.zip.sha256
   \`\`\`

3. Unzip into a dedicated folder outside a Git checkout, for example \`C:\Apps\OmniDrop\`.
4. Make sure \`python --version\` reports **Python 3.10+**, or configure \`OMNIDROP_PYTHON\`.
5. Launch **\`OmniDrop.exe\`**, drop a file and choose an available action.

The portable package bundles Qt and selected Python worker dependencies, **not the Python interpreter**. Optional translation language packs and voice models are separate add-ons. Windows may warn about the unsigned Alpha executable.

**Troubleshooting:** from the extracted folder, run \`.\omnidrop-cli.exe worker-ping\` to check whether the Python worker can start. See [release limitations](docs/releases/v0.2.0.md).

### Three ways to use it

**1. One-off file actions.** Drop an image, PDF, text file or ZIP. OmniDrop checks its type and runtime capabilities, then saves transformed output separately instead of overwriting the original.

**2. Visual workflows.** Create a sequence, switch to graph mode, connect branches and joins, preview type/dependency checks, then run and inspect results.

**3. Watched folders.** Explicitly arm one local directory for a chosen workflow. Only *new, stable* arrivals are considered; already-present files and generated outputs are suppressed. Watching is non-recursive and stops with the session.

<!-- VISUAL SLOT 03 / DAG BUILDER: assets/readme/workflow-canvas.webp
     Real graph-editor screenshot to be added later. -->

## Workflows

OmniDrop's workflow engine reuses the **same stable Action IDs** as the GUI and CLI. Definitions are portable \`.omniworkflow.json\` files, not embedded scripts or unchecked shell commands.

For example, [Clean and deduplicate text](workflows/examples/text-clean.omniworkflow.json):

\`\`\`text
Your TXT file
    │
    ▼
text.normalize
    │
    ▼
text.deduplicate
    │
    ▼
New output file (original unchanged)
\`\`\`

Run it from the extracted Windows package:

\`\`\`powershell
.\omnidrop-cli.exe workflow validate .\workflows\examples\text-clean.omniworkflow.json
.\omnidrop-cli.exe workflow run .\workflows\examples\text-clean.omniworkflow.json C:\Files\notes.txt
\`\`\`

Replace \`C:\Files\notes.txt\` with a real text file. The GUI also offers **Preview plan**, **Run workflow**, **Add to Jobs** and **Watch folder...**. The **Stop after current file** control takes effect at safe action boundaries, not mid-write.

**Important retry behavior:** Jobs retry runs the **whole workflow again** and retains previous output files. It does *not* resume from an arbitrary saved node. Folder watching does not auto-start when Windows boots.

[Workflow guide](docs/WORKFLOWS.md) · [Jobs and retries](docs/WORKFLOW_JOBS.md) · [Folder Watch safety](docs/WORKFLOW_WATCH.md) · [Example workflows](workflows/examples)

<!-- VISUAL SLOT 04 / JOBS + WATCH: assets/readme/jobs-history.webp and folder-watch.webp
     Reserved until real screenshots are available. -->

## Local-first, by design

- **No account for core actions.** Built-in file transforms operate on local files.
- **No invisible uploads.** Remote translation requires explicit user approval and is separate from local workflow definitions.
- **Keep originals.** Built-in transformations create new output paths; inspect or remove generated files yourself.
- **Predictable automation.** Workflows use an allowlist of actions; arbitrary shell commands and implicit network calls are not part of the workflow schema.
- **Honest dependencies.** Features requiring Pillow, pypdf, Argos, a Piper runtime or a voice pack are reported based on local availability.

Opt-in online providers have their own quotas and privacy policies. See [Security](SECURITY.md) and [translation privacy](docs/TRANSLATION.md).

## Platform support

| Platform | Current support |
| --- | --- |
| **Windows 10/11 x64** | Alpha desktop GUI + CLI portable release; Windows CI, tests and packaged smoke |
| **Linux** | Core/CLI build and tests in CI; no officially packaged desktop release yet |
| **macOS** | Not currently a supported release target |

## For developers

\`\`\`text
Qt desktop GUI ─┐
                ├─→ C++ application services + Action Catalog
CLI ────────────┘                  │
                                  ▼
                         Python/local adapters
                                  │
                                  ▼
                         New outputs + job state
\`\`\`

Build from source with **CMake 3.24+**, **C++20**, **Qt 6.5+** and **Python 3.10+**:

\`\`\`powershell
python -m pip install -r python/requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -m unittest discover -s tests -p "test_*.py"
\`\`\`

The C++ GUI and CLI share application services rather than duplicating business logic. Workers expose structured JSON responses and optional processors are runtime-gated.

[Architecture](docs/ARCHITECTURE.md) · [Action contracts](docs/ACTIONS.md) · [Contribution guide](CONTRIBUTING.md) · [CI results](https://github.com/stloendays/OmniDrop-Alpha/actions/workflows/build.yml)

## What's next?

OmniDrop is being built in public. Priorities include:

- A self-contained, signed Windows installer and a verified update/rollback path.
- More capable PDF/image tools, OCR and media adapters.
- Better workflow history, validated recovery, notifications and scheduling.
- An extensible plugin / agent integration boundary with stable callable interfaces.

These are **roadmap items, not promises of features in v0.2.0**. See the [detailed roadmap](docs/ROADMAP.md) and [open issues](https://github.com/stloendays/OmniDrop-Alpha/issues).

## Contributing and community

Bug reports, use cases, documentation fixes and small focused PRs are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md), check [existing issues](https://github.com/stloendays/OmniDrop-Alpha/issues), and include reproducible details and sanitized logs. For vulnerabilities, follow [SECURITY.md](SECURITY.md) instead of opening a public issue.

OmniDrop is released under the **[MIT License](LICENSE)**.

<sub>Made for useful local workflows. Illustrations and real product screenshots will be added once verified assets are ready.</sub>
