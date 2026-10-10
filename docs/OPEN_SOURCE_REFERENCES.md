# Open-source product references and OmniDrop implementation plan

OmniDrop learns from successful open-source tools without simply cloning their
interfaces, bundling unrelated dependencies, or copying unreviewed code.
Every new capability must use OmniDrop's stable callable interfaces, preserve
user files and pass Windows/Linux CI.

| Reference | Useful product idea | OmniDrop state | Approach |
| --- | --- | --- | --- |
| [Stirling-PDF](https://github.com/Stirling-Tools/Stirling-PDF) | PDF image extraction, batch document operations | **Implemented** `pdf.extract_images` | Independent pypdf/Pillow worker; safe generated output filenames; bounded output |
| [Czkawka](https://github.com/qarmin/czkawka) | Fast size/hash duplicate scan | **Implemented** `file.find_duplicates` | Independent Python standard-library size + SHA-256 report; no automatic removal |
| [PowerToys / PowerRename](https://github.com/microsoft/PowerToys) | Search/replace rename, regex, before/after preview and undo | Planned P1 | Start with a dry-run and conflict detection; require explicit confirmation and durable rollback journal before any mutation |
| [Files](https://github.com/files-community/Files) | Quick preview, keyboard commands, tags and dual-pane exploration | Planned P2 | Prefer a lightweight read-only preview and reliable action search over rebuilding Explorer |
| [Docling](https://github.com/docling-project/docling) | Structured PDF/Office parsing, tables, OCR with offline support | Planned P2, optional adapter | Gated model/runtime install with clear privacy/licensing and file-size controls; no default heavyweight models |
| [Czkawka](https://github.com/qarmin/czkawka) | Perceptual similarity scan and caching | Planned P2 | Optional image hashing with tunable threshold and manual review; do not auto-delete |

## Implementation boundaries

- Reference **feature behavior and UX ideas**, not code, UI assets or branding.
- Licenses differ by project, module and version (some products use mixed
  licensing). Review exact upstream source/license/attribution before any
  actual code reuse. The two implemented features above were written
  independently in OmniDrop.
- Never call an unimplemented feature available from the GUI/CLI.
- Do not force network access, unapproved model downloads, telemetry, or
  destructive cleanup to implement a convenient file action.
- Workflows reuse stable action IDs and perform compatibility checks before
  invocation. Mixed-file duplicate reports are advisory, not deletion.
- For bulk rename, implement a **preview first**: detect collisions, case-only
  rename issues, disallowed paths, missing/moved files and restart recovery
  before adding any commit/undo operation.

## Recommended next development order

1. **PowerRename-style preview:** selectable files, literal and regex
   replacements, dry-run diff table. Do not actually rename in this first cut.
2. **Safe commit/undo:** explicit confirmation, collision-safe temporary names,
   atomic operation journal and a verified reversible plan; never overwrite.
3. **Files-style quick view:** focused previews for TXT, PDF and images with
   lazy loading and predictable memory limits, not a new general file browser.
4. **Docling integration spike:** on-demand optional document layout/OCR
   adapter, verified model/runtime provenance and a local-only mode.
5. **Similar-image review:** perceptual hash groups, preview and manual
   decisions, with caching isolated from document contents.

See [ROADMAP.md](ROADMAP.md) for the canonical product roadmap and
[ACTIONS.md](ACTIONS.md) for actually supported action IDs.
