# ASTRA

ASTRA is a C++20 nixpkgs-backed application package manager under development. The current build is **0.3.0-dev**: CLI, private SQLite state, pinned revision indexing, cached search/info, and per-user managed profile transactions. Install, remove, and rollback are exercised with a disposable fake Nix backend; live Nix integration is unverified. Commands that have no implementation fail explicitly.

ASTRA owns repository policy, transaction records, and managed profiles; Nix is intended to own evaluation, builds, the store, binary substitution, and supported profile operations. No Almagest-G native build or Nix integration has been validated here. See [MILESTONES.md](MILESTONES.md) for acceptance evidence.

Build locally with `make -j2` and run `make check`. No build step downloads dependencies. See [BUILDING.md](BUILDING.md).

After a live Nix backend is validated, use `astra repo update`, then `astra --yes install hello jq`. Review the printed Nix build preview before supplying `--yes`; interactive use asks for confirmation. `astra list`, `astra history`, `astra history show ID`, `astra upgrade`, and `astra rollback` operate on ASTRA's own `default` profile. Choose another per-user profile with `--profile NAME`. The managed executable path is `$XDG_STATE_HOME/astra/profiles/NAME/profile/bin` (or `$HOME/.local/state/astra/profiles/NAME/profile/bin` when XDG_STATE_HOME is unset); add it to your shell PATH explicitly if desired. ASTRA does not alter your existing Nix profiles or shell configuration.

See [TRANSACTIONS.md](TRANSACTIONS.md) for hash construction, activation, recovery, and rollback limits.

Licensed GPL-3.0-or-later. Vendored SQLite is public domain; see [vendor/sqlite/PROVENANCE.md](vendor/sqlite/PROVENANCE.md).
