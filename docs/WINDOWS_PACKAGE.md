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
- Python worker dependencies under `python/vendor` (Python 3.13 Windows wheels);
- verified official CPython 3.13.16 under `runtime/python`;
- repository license/readme/assets installed by CMake.

## Python in newly built packages

This Windows packaging pipeline includes an official embedded CPython 3.13
interpreter, verified against a pinned SHA-256, and Pillow/pypdf dependencies
installed for the same Python 3.13 ABI. Newly built packages no longer require
system Python. An explicit `OMNIDROP_PYTHON` override remains available.

**Already published v0.2.0 ZIPs are unchanged** and still require Python
installed on the target system. See [Python runtime](PYTHON_RUNTIME.md).

## CI smoke test

Before the ZIP is uploaded, CI checks the staged package itself rather than only the build tree:

1. required Qt runtime DLLs and `platforms/qwindows.dll` exist;
2. the packaged CLI starts and reports the expected version;
3. the isolated interpreter imports Pillow/pypdf and CLI worker ping succeeds with Python removed from PATH;
4. the packaged GUI remains running after startup;
5. a second packaged GUI launch forwards to the primary instance and exits within a bounded time;
6. the primary instance remains alive after that activation;
7. the primary window is then closed cleanly (with forced cleanup only as a CI fallback).

The final ZIP is accompanied by a SHA-256 checksum. Main-branch CI also
creates a signed, verifiable GitHub artifact provenance attestation, and
the release publisher requires this attestation to match its exact main commit.
Optional Windows Authenticode signing requires a trusted certificate and
explicit credentials. See [Signing](CODE_SIGNING.md).
