# Update awareness — read-only release checking (development source)

The update subsystem remains **read-only**. OmniDrop can
compare its compiled version against verified metadata from the official
[GitHub Releases](https://github.com/stloendays/OmniDrop-Alpha/releases)
endpoint, but it **does not** download, install, stage, unzip, restart, replace
executables or modify any local files. Development builds can also preflight
an already downloaded ZIP and its SHA-256 file locally.

This feature is in the development source; it is **not in the tagged v0.4.0
Alpha ZIP** until a newer, fully tested release is published.

## Desktop

Open **Help > Check for updates...**. The dialog does not connect to the
internet on opening. Choose:

- **Alpha (includes stable)** for the current early-access distribution; or
- **Stable only** to ignore prereleases.

Click **Check now** to contact
`https://api.github.com/repos/stloendays/OmniDrop-Alpha/releases?per_page=30`.
An eligible newer version provides **View release in browser**. The link is
constructed exclusively from a strictly validated version tag, under
`https://github.com/stloendays/OmniDrop-Alpha/releases/tag/`; the UI never
opens a link copied from untrusted release body text.

The check reads only published release metadata. It does not upload files,
document titles, selected paths, account credentials or worker output.

## CLI

```powershell
.\omnidrop-cli.exe update-check
.\omnidrop-cli.exe update-check --channel alpha
.\omnidrop-cli.exe update-check --channel stable
```

Explicit invocation returns JSON with:

```json
{
  "schema_version": 1,
  "operation": "update.check",
  "ok": true,
  "state": "update_available",
  "channel": "alpha",
  "installed_version": "0.4.0",
  "available_version": "0.5.0",
  "release_url": "https://github.com/stloendays/OmniDrop-Alpha/releases/tag/v0.5.0",
  "prerelease": true,
  "error": "",
  "auto_install_supported": false
}
```

*Illustrative response only: it does not assert that version 0.5.0 exists.*

States are `update_available`, `up_to_date`, `no_release`, or `error`.
Unknown channels and malformed installed versions are rejected before any
network access. The default channel is `alpha` for this Alpha distribution.

## Metadata eligibility and network constraints

A release is considered eligible only when:

- its tag matches exactly `vMAJOR.MINOR.PATCH` (canonical numeric SemVer,
  without suffixes);
- it is published, not a draft;
- it is permitted by the explicitly requested channel;
- the release metadata lists exactly one expected
  `OmniDrop-VERSION-windows-dev.zip` and one corresponding `.sha256` asset,
  both with the expected official GitHub download URLs and plausible sizes.

Versions are compared numerically, never by the order in the feed or by text
sorting. This is a **metadata check only**: observing a SHA-256 asset is not
equivalent to downloading and verifying it. The checker does not claim that a
package has a valid signature, is safe to install, or is already installed.

Only the HTTPS GitHub API host is contacted, and only when explicitly asked.
The client rejects redirects, bounds response size to 1 MiB, applies a network
request timeout and has a 15-second overall deadline. Malformed JSON,
unexpected HTTP status, TLS issues and unavailable network return
user-visible errors. There is no background polling, retry loop or telemetry.

## Local ZIP integrity and structural preflight

The development GUI offers **Help > Check for updates... >
Verify downloaded ZIP...**. Select the already-downloaded official ZIP; its
neighboring `.sha256` is selected automatically when present, otherwise a file
picker asks for the matching checksum. The operation runs off the UI thread,
reads local files only and does not contact GitHub.

The same C++ application service is callable from CLI:

```powershell
.\omnidrop-cli.exe update-verify "C:\Downloads\OmniDrop-0.5.0-windows-dev.zip" "C:\Downloads\OmniDrop-0.5.0-windows-dev.zip.sha256"
.\omnidrop-cli.exe update-verify "C:\Downloads\OmniDrop-0.5.0-windows-dev.zip" "C:\Downloads\OmniDrop-0.5.0-windows-dev.zip.sha256" --install-dir "C:\Apps\OmniDrop"
```

Version 0.5.0 is illustrative, not an announcement. Supply absolute paths.
When `--install-dir` is absent, the running executable's directory is checked.
The directory must already exist, must not have symlinked ancestors, and no
parent or itself may contain `.git` (including worktree `.git` files).
This is a **preflight guard only**, not an installer.

The result JSON includes `operation: "update.verify-local"`,
`schema_version: 1`, `package_version`, `archive_sha256`,
`archive_bytes`, `entry_count`, `target_safe`, `error`, and safety flags.

The local verifier:

- requires the canonical `OmniDrop-VERSION-windows-dev.zip` basename and
  its exact matching `.sha256` sidecar, containing one SHA-256 hash and filename;
- streams SHA-256 over at most 1 GiB and refuses mismatches or source symlinks;
- inspects ZIP central/local directory consistency without extracting anything;
- rejects absolute/traversal/Windows-reserved/ambiguous paths, case-insensitive
  collisions, symlinks and special entries, encrypted entries, ZIP64,
  unsupported compression, incomplete archives, and implausible expanded sizes;
- requires root `OmniDrop.exe` and `omnidrop-cli.exe`.

**Important trust boundary:** A matching ZIP and checksum obtained together
do **not** authenticate the publisher. This feature does not verify GitHub
attestation, signing identities, internal file hashes after extraction or
Windows Authenticode signatures. Its successful result means only **local
checksum agreement and basic structural screening**. Never treat it as
authorization to execute or install the package. A fully trusted updater must
independently bind its package to a verified signed release identity.

## Planned verified update flow

A future **separate implementation** must add trusted release manifests,
managed-file hashes, signed release identity and provenance verification;
staging outside the active installation; safe ZIP extraction and post-extraction
file verification; a separate Windows apply helper after process exit;
installed-build identity; last-known-good backup/rollback; and explicit
release-channel policy. Archive structure checks and `.git` guards exist in
local preflight but are **not** yet wired to any installer. No existing
`.git` worktree may be replaced.

Until then, install upgrades manually using the published checksum file and
GitHub attestation as explained in [Windows packaging](WINDOWS_PACKAGE.md).
