# Update awareness — read-only release checking (development source)

The first update subsystem is deliberately **read-only**. OmniDrop can
compare its compiled version against verified metadata from the official
[GitHub Releases](https://github.com/stloendays/OmniDrop-Alpha/releases)
endpoint, but it **does not** download, install, stage, unzip, restart, replace
executables or modify any local files.

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

## Planned verified update flow

A future **separate implementation** must add trusted release manifests,
managed-file hashes and signature verification; staging outside the active
installation; archive traversal protection; package version checks; a separate
Windows apply helper after process exit; installed-build identity;
last-known-good backup/rollback; explicit release-channel policy; and
development checkout safeguards. No existing `.git` worktree may be replaced.

Until then, install upgrades manually using the published checksum file and
GitHub attestation as explained in [Windows packaging](WINDOWS_PACKAGE.md).
