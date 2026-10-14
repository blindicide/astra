# ASTRA campaign acceptance ledger

This ledger tracks evidence, not release claims. A checked item requires a real passing gate. Almagest-G and Nix are unavailable on this development host; target and live-backend gates remain unverified until those environments exist.

| Milestone | Scope | Status |
| --- | --- | --- |
| v0.1.0 | Build, CLI, SQLite schema, backend detection | In progress |
| v0.2.0 | Pinned nixpkgs revisions, indexed metadata, search/info | Pending |
| v0.3.0 | Multi-package install/removal, managed profile | Pending |
| v0.4.0 | Canonical transaction IDs, journal, activation | Pending |
| v0.5.0 | Generations, rollback, crash reconciliation | Pending |
| v0.6.0 | Almagest-G integration and Nix bootstrap | Pending; target unavailable |
| v0.7.0 | Upgrade, update, why, cleanup | Pending |
| v0.8.0 | Security, fault injection, repair, diagnostics | Pending |
| v0.9.0 | Integration tests, docs, compatibility freeze | Pending |

## Specification §14 gates

| # | Acceptance gate | Evidence status |
| --- | --- | --- |
| 1 | Offline source archive builds on conventional Linux | Pending |
| 2 | Mandatory automated tests pass | Pending |
| 3 | Supported Nix backend operational and detected | Unverified; no Nix installed |
| 4 | Search and package metadata work | Pending |
| 5 | Multi-package atomic profile transaction | Pending |
| 6 | Removal and upgrade work | Pending |
| 7 | Immutable transaction hash and audit trail | Pending |
| 8 | Rollback restores managed profile | Pending |
| 9 | Power-loss fault injection reconciles every state | Pending |
| 10 | GC protects active and rollback generations | Pending |
| 11 | Untrusted cache outputs rejected | Pending |
| 12 | Native Almagest-G build passes | Unverified; no target |
| 13 | Almagest-G Nix bootstrap validated | Unverified; no target |
| 14 | Almagest-G operation preserves protected OS components | Unverified; no target |
| 15 | Documentation matches implementation | Pending |
| 16 | Verified source/bootstrap GitHub release assets | Pending; publication prohibited until all gates pass |
