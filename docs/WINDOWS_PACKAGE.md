# Windows development package

The Windows CI artifact is intended to behave like a portable development build rather than a
developer build-tree snapshot.

## Included

The staging workflow installs OmniDrop, then runs `windeployqt` against `OmniDrop.exe`. The
resulting ZIP therefore includes the Qt runtime libraries and the Windows platform plugin required
to start the desktop application on another Windows machine.

The package also contains:

- `OmniDrop.exe`;
- `omnidrop-cli.exe`;
- the local Python worker;
- Python worker dependencies under `python/vendor`;
- repository license/readme/assets installed by CMake.

## Current Python requirement

The alpha package does **not** yet embed a Python runtime. It still requires a compatible Python
installation that can be resolved as `python`, or an explicit `OMNIDROP_PYTHON` override.

Bundling worker dependencies is still useful: users do not need to separately install Pillow or
pypdf into their global Python environment.

A future release package should embed or otherwise own its Python runtime before OmniDrop is
described as fully self-contained.

## CI smoke test

Before the ZIP is uploaded, CI checks the staged package itself rather than only the build tree:

1. required Qt runtime DLLs and `platforms/qwindows.dll` exist;
2. the packaged CLI starts and reports the expected version;
3. the packaged CLI can start and ping the local Python worker;
4. the packaged GUI remains running after startup;
5. a second packaged GUI launch forwards to the primary instance and exits within a bounded time;
6. the primary instance remains alive after that activation;
7. the primary window is then closed cleanly (with forced cleanup only as a CI fallback).

The final ZIP is accompanied by a SHA-256 checksum file.
