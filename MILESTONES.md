# ASTRA campaign acceptance ledger

This ledger tracks evidence, not release claims. A checked item requires a real passing gate. Almagest-G and Nix are unavailable on this development host; target and live-backend gates remain unverified until those environments exist.

| Milestone | Scope | Status |
| --- | --- | --- |
| v0.1.0 | Build, CLI, SQLite schema, backend detection | Local tests pass; live backend unavailable |
| v0.2.0 | Pinned nixpkgs revisions, indexed metadata, search/info | Fake-backend tests pass; live indexing unverified |
| v0.3.0 | Multi-package install/removal, managed profile | Fake-backend tests pass; live profile operations unverified |
| v0.4.0 | Canonical transaction IDs, journal, activation | Local hash/journal checks pass; live switch durability unverified |
| v0.5.0 | Generations, rollback, crash reconciliation | Fake-backend rollback and twelve fault boundaries pass; live recovery unverified |
| v0.6.0 | Almagest-G integration and Nix bootstrap | Pending; target unavailable |
| v0.7.0 | Upgrade, update, why, cleanup | Fake-backend tests pass, including abandoned-link cleanup; live Nix unverified |
| v0.8.0 | Security, fault injection, repair, diagnostics | Root restoration and journal-tamper refusal pass fake-backend tests; remaining hardening pending |
| v0.9.0 | Integration tests, docs, compatibility freeze | Pending |

## Specification §14 gates

| # | Acceptance gate | Evidence status |
| --- | --- | --- |
| 1 | Offline source archive builds on conventional Linux | Pending |
| 2 | Mandatory automated tests pass | Pending |
| 3 | Supported Nix backend operational and detected | Unverified; no Nix installed |
| 4 | Search and package metadata work | Fake-backend tests pass; live Nix unverified |
| 5 | Multi-package atomic profile transaction | Fake-backend test passes; live Nix unverified |
| 6 | Removal and upgrade work | Removal and upgrade fake-backend tests pass; live Nix unverified |
| 7 | Immutable transaction hash and audit trail | Local hash/journal, history inspection, chain/selection audit, and SQLite trigger tests pass; live durability unverified |
| 8 | Rollback restores managed profile | Fake-backend test passes; live Nix unverified |
| 9 | Power-loss fault injection reconciles every state | Twelve boundary tests pass; full-disk, permission, live power loss pending |
| 10 | GC protects active and rollback generations | Root registration mocked and audited; live Nix GC unverified |
| 11 | Untrusted cache outputs rejected | Pending |
| 12 | Native Almagest-G build passes | Unverified; no target |
| 13 | Almagest-G Nix bootstrap validated | Unverified; no target |
| 14 | Almagest-G operation preserves protected OS components | Unverified; no target |
| 15 | Documentation matches implementation | Pending |
| 16 | Verified source/bootstrap GitHub release assets | Pending; publication prohibited until all gates pass |
