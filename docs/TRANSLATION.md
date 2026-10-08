# Translation and privacy

OmniDrop's first translation workflow supports UTF-8 `*.txt`, `*.md`,
`*.markdown`, `*.srt` and `*.vtt`.

### Interfaces

- Desktop: **Translate...** in the header or **Tools -> Translate text or subtitles...**
- CLI: `omnidrop-cli translate document.md --from en --to zh --provider argos`
- CLI with explicit upload consent:
  `omnidrop-cli translate document.srt --from en --to zh --provider mymemory --allow-upload`
- Worker v1 additive command:
  `{"command":"translate_file","path":"C:/a.txt","source_lang":"en","target_lang":"zh","provider":"mymemory","allow_remote":true}`
- C++: `TranslationService::translateFile(const TranslationRequest&)` is the shared operation used by GUI and CLI.

### Providers

| Provider | Mode | Credentials | Notes |
| --- | --- | --- | --- |
| `argos` | offline | none | Requires `argostranslate` and an installed language pair (optional add-on) |
| `mymemory` | cloud | no key required | Free quota is small. Use `OMNIDROP_MYMEMORY_EMAIL` for the documented higher daily allowance; not unlimited |
| `libretranslate` | cloud or self-hosted | depends on server | Defaults to `http://127.0.0.1:5000/translate` (local self-hosted service), or use `--endpoint`; HTTPS required for remote hosts |
| `deepl-free` | cloud | API Free key | `OMNIDROP_DEEPL_API_KEY` or ephemeral key in desktop dialog. The key is never passed through CLI flags |

Provider availability and quota depend on the user's own deployment/account.
Translation is strictly opt-in and no background auto-upload is performed.
No telemetry or credential storage is introduced.

### Behavior and limitations

- The user must approve each remote operation; offline Argos needs no approval.
- The tool translates segments; it does not send original filenames or filesystem paths to remote providers.
- Source files are untouched. Only after **all** segments succeed is a new sibling file written.
- For Markdown, heading/list prefixes and fenced code are preserved. Inline-code/complex Markdown is left untouched rather than risk corrupting it.
- SRT/VTT timestamps and numeric cue IDs are preserved, but multi-line speaker segmentation and complex VTT cue metadata are not yet fully modeled.
- This version does **not** preserve DOCX/PDF layout; full document translation is a separate feature.
- Explicit bounds: 100 KB input, at most 250 segments, maximum 40,000 input characters online; MyMemory has a stricter conservative quota preflight.
- Network timeout is 15 seconds per request. Provider errors are redacted and do not include document text or API keys.
- Output is stored in the input folder under a unique `translated-<language>` suffix with restrictive local permissions where supported.
- Never translate confidential documents through cloud providers without understanding the provider's privacy terms.

Optional offline installation:

```powershell
python -m pip install argostranslate
# Then install the required language package using the official Argos package manager.
```

The portable package still needs its Python worker runtime. Future standalone builds
will treat optional models, voices and language packs as independently versioned add-ons.
