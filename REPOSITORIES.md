# Repository policy

The development build has one approved upstream, `github:NixOS/nixpkgs/nixos-26.05`. `astra repo update` asks Nix for flake metadata, requires a `github` lock owned by `NixOS/nixpkgs`, a 40-character immutable Git revision, and a SHA-256 NAR hash. It then runs `nix search --json` against the **locked revision**, indexes package name/version/description into SQLite, and changes the selected revision only after the complete import commits. `astra update` is an alias. It does not upgrade installed packages.

`search` and `info` read only the indexed revision; they do not evaluate nixpkgs for every query. The index does not prove a package can be built, substituted, or integrated on Almagest-G. License is currently not indexed, and `info` reports it as unknown. `--offline` prohibits repository updates; cached queries work offline. A failed identity, hash, catalog, or cache-trust check leaves the selected pin unchanged.

The default upstream branch is an update source, **not** transaction provenance. Future package selections must record the exact locked revision and output paths. Overlays are not implemented yet. No third-party binary caches are configured or trusted by ASTRA.

The JSON interfaces used here are documented in the [Nix flake metadata manual](https://nix.dev/manual/nix/2.28/command-ref/new-cli/nix3-flake-metadata) and [Nix search manual](https://nix.dev/manual/nix/2.28/command-ref/new-cli/nix3-search). The Nix manual labels these commands experimental, so live compatibility and output shape must be tested before a release claim.
