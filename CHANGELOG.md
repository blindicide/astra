# Changelog

## 0.3.0-dev

- Add per-user managed profiles with install, removal, listing, and auditable rollback.
- Add SHA-256 canonical transaction intents, private durable journals, profile locks, retained generation roots, and reconciliation around activation.
- Add SQLite schema migration backup, immutable intent triggers, deterministic fake-backend integration tests, and twelve crash-boundary tests.
- Add pinned-revision upgrades, detailed history inspection, and audit checks against the recorded generation chain and package selections.
- Retain the locked nixpkgs NAR hash with each revision and reject conflicting metadata for an existing revision.
- Live Nix and Almagest-G validation remain outstanding.

## 0.2.0-dev

- Add pinned nixpkgs revision lookup and atomic metadata indexing through Nix JSON interfaces.
- Add cached `repo list`, `repo update`, `search`, and `info` commands with versioned JSON output.
- Reject untrusted repository identities and malformed revision/content hashes.

## 0.1.0 development

- Add offline C++20 and vendored SQLite build.
- Add CLI help, version, state integrity diagnostics, and Nix executable version detection.
- Reject package commands until implemented.
