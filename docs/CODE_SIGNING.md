# Windows signing and verifiable release provenance

These are **two independent guarantees**; do not label an unsigned executable
as Authenticode-signed just because a hash or build provenance exists.

## 1. GitHub build provenance (automatic, no private key)

After a successful **main-branch** Windows build, the build workflow creates a
GitHub artifact attestation signed via short-lived Sigstore identity for the
final portable ZIP. Release publication verifies the attestation against
`stloendays/OmniDrop-Alpha/.github/workflows/build.yml`, the source commit,
and the `main` ref. The release pipeline also checks its SHA-256 and required
archive contents.

```powershell
gh attestation verify .\OmniDrop-0.3.0-windows-dev.zip --repo stloendays/OmniDrop-Alpha --signer-workflow stloendays/OmniDrop-Alpha/.github/workflows/build.yml
```

Replace the example filename with your downloaded ZIP. An attestation verifies
its producing workflow and artifact digest; it does **not** give the EXE a
Windows Trusted Publisher identity or guarantee that its behavior is safe.

## 2. Windows Authenticode (certificate-dependent)

`scripts/sign_windows.ps1` signs the two OmniDrop-owned executables before ZIP
hashing/attestation, using SHA-256 Authenticode and an RFC-3161 timestamp. It
then uses Windows SDK `signtool verify /pa` to enforce successful verification.
Upstream CPython and Qt binaries are not re-signed.

The repository owner must obtain a publicly trusted **code-signing certificate
with an exportable PFX/private key** and set these GitHub Actions **repository
secrets**, never commit them:

- `OMNIDROP_CODESIGN_PFX_BASE64`: Base64-encoded bytes of the PFX file.
- `OMNIDROP_CODESIGN_PASSWORD`: password protecting that PFX.

Use GitHub **Settings → Secrets and variables → Actions**. Never store a PFX,
unencrypted private key or password in repository files, issue comments,
artifacts or logs. Modern hardware-bound signing certificates that cannot be
exported require a separate HSM/managed-signing integration, not this PFX path.

If **both secrets are absent**, the development build is still produced,
but the EXEs remain **unsigned**; CI emits a warning. If only one secret is
provided or signing/verification fails, packaging fails closed. The signing
certificate is imported into the Windows runner's temporary user store and
removed afterwards; the temporary PFX file is also removed.

To check a signed Windows executable locally:

```powershell
Get-AuthenticodeSignature .\OmniDrop.exe | Format-List Status,SignerCertificate
Get-AuthenticodeSignature .\omnidrop-cli.exe | Format-List Status,SignerCertificate
```

`Status = Valid` requires a trusted certificate chain on that machine.
Signing does not guarantee immediate Microsoft SmartScreen reputation.
No certificate, trusted publisher identity or Authenticode signature is
included with this PR.

## Release invariant

A feature branch may generate an unsigned development ZIP for tests.
The publisher can only release the successful, attested package from the
current canonical `main` commit, and only after a matching version/release
request. Previously published releases are never silently replaced.
