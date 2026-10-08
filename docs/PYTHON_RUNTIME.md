# Bundled Python runtime (Windows x64)

OmniDrop is a Qt/C++ host using a **separate, stateless** Python worker, not
an embedded CPython C API in the UI process. The Windows portable CI package
includes its own Python interpreter and its Pillow/pypdf worker dependencies.
Users of a newly built package do not need a system Python installation.

**Existing v0.2.0 releases published before this change remain unchanged.**
Only packages produced by the updated build workflow contain this runtime.

## Install layout

```text
OmniDrop.exe
omnidrop-cli.exe
python/
  worker.py
  translation.py
  workflow_engine.py
  vendor/                     # Pillow + pypdf, installed by CI
runtime/
  python/
    python.exe
    python313.dll
    python313.zip
    python313._pth            # isolated import search paths
    LICENSE.txt               # CPython upstream license
```

The runtime is official **CPython 3.13.16 (Windows amd64 embeddable)**.
`scripts/stage_embedded_python.ps1` downloads it at *build time only* from
`python.org` and rejects the archive if the published SHA-256 does not match
`97dae5274cc54867065e8d5a3226e48c35017ed332a0fdb0e27d5b5821961297`.
No download, installation, registry modification or administrator privilege
is required on user startup.

The interpreter's `python313._pth` exposes the standard-library archive, the
runtime directory, OmniDrop's worker directory and the packaged vendor folder.
It does **not** import `site`: user site packages, registry paths, `PYTHONPATH`
and executable search order cannot silently substitute dependency sources.
Python's pip module is intentionally not included in the embedded runtime.

## Resolution contract

`PythonWorkerClient` resolves Python once per worker launch:

1. Nonempty `OMNIDROP_PYTHON` environment override (explicit).
2. Windows: `<directory of OmniDrop executable>/runtime/python/python.exe`
   when present.
3. Windows development fallback: `python` on PATH.
4. Other operating systems: `python3` on PATH.

`OMNIDROP_WORKER` still selects an explicit worker script; otherwise the
shipped `python/worker.py` or existing development paths are used.
GUI and CLI use the same adapter and unchanged Worker Protocol v1.
The interpreter runs as a child process, not inside Qt's address space.
The worker uses `-B` to avoid attempting to write bytecode into the install
directory. Use `omnidrop-cli worker-ping` and `omnidrop-cli capabilities` to
inspect availability after installation.

## Optional processors

Pillow and pypdf ship with the main Windows artifact. Piper TTS and Argos
Translate remain optional and are **not** automatically installed or enabled.
If an optional native Python package is required, provide compatible
CPython-3.13 Windows wheels in `python/vendor` or explicitly override
`OMNIDROP_PYTHON` to a managed environment; do not install packages into
the embedded Python binary using an unsupported pip bootstrap.

The bundled Python version and its SHA-256 pin must be updated together;
stage, isolated import, GUI/CLI packaged smoke tests and artifact provenance
must pass again before publishing an upgraded runtime.
