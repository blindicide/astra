# ASTRA

ASTRA is a C++20 nixpkgs-backed application package manager under development. The current build is **0.2.0-dev**: CLI, private SQLite state, Nix executable detection, pinned revision indexing, and cached search/info. Live Nix integration is unverified. It does not yet install packages. Commands that have no implementation fail explicitly.

ASTRA owns repository policy, transaction records, and managed profiles; Nix is intended to own evaluation, builds, the store, binary substitution, and supported profile operations. No Almagest-G native build or Nix integration has been validated here. See [MILESTONES.md](MILESTONES.md) for acceptance evidence.

Build locally with `make -j2` and run `make check`. No build step downloads dependencies. See [BUILDING.md](BUILDING.md).

Licensed GPL-3.0-or-later. Vendored SQLite is public domain; see [vendor/sqlite/PROVENANCE.md](vendor/sqlite/PROVENANCE.md).
