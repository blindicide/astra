# Testing and evidence

Run `make -j2 && make check` on a conventional Linux host with the documented
build dependencies. `make check` covers CLI refusals, private SQLite state and
migration backups, pinned repository parsing, profile operations, canonical
hashes, rollback, tamper detection, and fourteen transaction fault boundaries.
The fake Nix backend also rejects disabled signatures, unknown keys and
substituters, failed signatures, and attempted cache access in offline mode.

After a reviewed commit, `scripts/package-source-candidate.sh OUTPUT-DIR`
generates a `.tar.zst` source candidate and SHA-256 file from `HEAD`.
`scripts/check-source-archive.sh ARCHIVE` extracts it in a temporary tree,
runs `make -j2`, `make check`, a staged `make install`, `astra --version`, and
the expected missing-Nix `astra doctor` diagnostic. Run the check without
network access to validate offline compilation. These are candidate artifacts,
not a v1.0.0 release.

An AddressSanitizer and UndefinedBehaviorSanitizer build must also pass before
release. Live Nix, multi-user daemon, cache signatures, GC retention, and
Almagest-G native build/bootstrap tests are pending. The acceptance ledger in
[MILESTONES.md](MILESTONES.md) records which evidence is local or missing.
