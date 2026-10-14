# ASTRA per-user transactions (development)

`astra install`, `remove`, `upgrade`, and `rollback` change only an ASTRA-managed per-user application profile. They do not alter an existing Nix profile, system service, kernel, bootloader, system C library, PAM configuration, or application data. Rollback restores selected executables and package files in the managed profile; it does not restore user data, service databases, or OS configuration.

## Preparation and activation

ASTRA locks one named profile, reconciles prior prepared work, and resolves package selections against pinned repository revisions. It shows the proposed package count and a Nix build preview, then requires confirmation unless `--yes` was supplied. Nix builds or substitutes missing package outputs. ASTRA asks Nix to construct a complete staged profile, verifies its store path, and registers a separate Nix GC root for the target. A single `nix-env --profile ... --set <target-store-path>` call activates the new managed profile generation. ASTRA verifies the active store reference before marking the transaction committed. The actual Nix behavior has not been validated on this development host.

The profile lives below `$XDG_STATE_HOME/astra/profiles/NAME/profile` or `$HOME/.local/state/astra/profiles/NAME/profile`. Each retained committed generation has a root below `roots/`; do not move these root paths because Nix records their locations. Staged profiles and roots created before a failed preparation may remain until a future explicit cleanup implementation; ASTRA does not silently delete them.

## Intent identity and audit

An intent is a UTF-8 JSON object serialized without whitespace by json-c. Keys are inserted in this fixed order: `schema_version`, `nonce`, `sequence`, `previous_hash`, `operation`, `profile`, `previous_store`, `repository_revision`, `target_store`, `restored_generation`, `selections`. `selections` is sorted by attribute, and each element has `attribute`, `revision`, and `output` keys in that order. The nonce is 128 random bits. SHA-256 of these exact bytes is the immutable full transaction hash. The usual displayed ID is its last eight hexadecimal characters; ASTRA extends it in four-character steps when another transaction has the same suffix. The database rejects updates to a stored intent/hash and retains journal events separately. The same immutable intent is stored in a private, synchronized journal file named with the full hash.

`history` shows committed and recovered-not-applied transactions. `history show ID` displays the full hash, immutable intent, generation relationship, and outcome events; `--json` emits schema version 1. ASTRA compares the active profile link, database generation, retained GC root, journal bytes, SHA-256 intent hash, previous-hash chain, and stored selections before accepting another mutation. A prepared transaction with the old active target is marked not applied. A prepared transaction whose target is active is marked committed after verification. An unexpected active target or corrupt journal requires operator intervention; ASTRA refuses further profile changes.

Journal files use create/write/fsync/rename/directory-fsync. SQLite uses full synchronous mode. These measures are designed for crash recovery but have only been exercised through process-exit fault injection with a fake backend, not power removal on a real filesystem or Nix host.

## Development limitations

- Only per-user profiles are implemented. A privileged system applications profile is pending.
- `upgrade` resolves installed selections at the newly selected repository revision and creates one profile transaction. `why`, `clean`, and detailed `repair` remain pending.
- Offline install/remove is rejected; offline rollback can use retained store paths. Search and info work from the indexed database offline.
- Nix profile command compatibility, binary cache signatures, daemon behavior, and live GC retention are unverified here because Nix is absent.
