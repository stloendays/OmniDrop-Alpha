# OmniDrop README visual assets

The repository includes four compact, self-contained SVG interface previews,
designed for the GitHub homepage and readable on desktop/mobile:

| Asset | Purpose |
| --- | --- |
| [desktop-preview.svg](desktop-preview.svg) | Main file workspace and context-aware PDF actions |
| [workflow-preview.svg](workflow-preview.svg) | Workflow Builder with valid offline DAG branches |
| [jobs-preview.svg](jobs-preview.svg) | Durable Jobs list and opt-in folder monitoring |
| [social-preview.svg](social-preview.svg) | Consistent brand/share identity image |

They deliberately use the same restrained black/white/gray design language
as OmniDrop. These are interface previews, **not claimed to be pixel-identical
captures** of the current Windows build. They make no promises about features
not yet supported by the `v0.3.0` release.

## Replacing these with captured images

Real Windows screenshots can replace or complement the previews once verified:

- `hero.webp`: clean full product window, 16:9.
- `workspace.webp`: file action selection, 16:9.
- `workflow-canvas.webp`: editable graph, 16:9.
- `jobs-history.webp`: retained Jobs history, 16:9.
- `folder-watch.webp`: opt-in folder monitor state, 16:9.

Before introducing those files, capture a test-only workspace, remove personal
paths/documents, confirm the controls are present in a runnable build, and
compress without sacrificing text clarity. README image links should point
only to committed files; use descriptive alt text.

The social identity SVG is suitable for inline README use. GitHub's separate
**Social preview** setting may require a raster PNG uploaded by a repository
owner; placing an SVG in source control does not configure that GitHub setting.
