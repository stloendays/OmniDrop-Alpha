# Security policy

OmniDrop is a local-first file utility. Security-sensitive behavior includes file parsing,
external process execution, future shell integration, plugins, updater logic, and any future
network or AI action.

Please do not publish a suspected vulnerability as a public issue. Use GitHub's private security
reporting feature once enabled for the repository.

Core rules for contributors:

- never upload a user's file implicitly;
- never execute file contents as code merely because a file was dropped;
- avoid shell-string command construction when launching processors;
- validate output paths and keep transforms non-destructive by default;
- treat third-party plugins and future update packages as untrusted until verified;
- do not let development Git checkouts self-overwrite through an updater.
