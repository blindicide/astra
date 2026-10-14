# Testing and evidence

Run `make -j2 && make check` on a conventional Linux host with the documented
build dependencies. `make check` covers CLI refusals, private SQLite state and
migration backups, pinned repository parsing, profile operations, canonical
hashes, rollback, tamper detection, fourteen transaction fault boundaries,
simulated full-disk journal failures, directory permission refusal, and doctor
diagnostics. The journal tests verify agreement between the active profile and
the committed database state after recovery.
The fake Nix backend also rejects disabled signatures, unknown keys and
substituters, failed signatures, and attempted cache access in offline mode.

After a reviewed commit, `scripts/package-source-candidate.sh OUTPUT-DIR`
generates a `.tar.zst` source candidate and SHA-256 file from `HEAD`.
`scripts/check-source-archive.sh ARCHIVE` extracts it in a temporary tree,
runs `make -j2`, `make check`, a staged `make install`, `astra --version`, and
the expected missing-Nix `astra doctor` diagnostic. Run the check in a network
isolated environment to prove offline compilation; the development-host check
only established that its build did not download dependencies. These are candidate artifacts,
not a v1.0.0 release.

`make check-sanitizers` builds an isolated copy with AddressSanitizer and
UndefinedBehaviorSanitizer and runs the same suite. CI runs this gate on every
push and pull request. Live Nix, multi-user daemon, cache signatures, GC retention, and
Almagest-G native build/bootstrap tests are pending. The acceptance ledger in
[MILESTONES.md](MILESTONES.md) records which evidence is local or missing.
