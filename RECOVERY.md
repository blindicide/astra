# Recovery runbook

Stop concurrent ASTRA commands for the affected profile. Preserve the private
state directory and its SQLite backup, journals, roots, and profile links
before attempting repair. Run `astra verify` and `astra history`; a pending
transaction is reconciled against the actual active profile. A prepared
transaction with the old link is recorded as not applied; one whose target is
active is recorded as committed only after verification. Any other target,
missing or altered journal, or inconsistent generation chain requires
operator investigation.

Run `astra --dry-run repair` to inspect missing committed GC roots and pending
work. `astra --yes repair` restores only a root whose stored path and intent
still agree, then audits again. It does not rewrite immutable transaction
intents or arbitrary SQLite corruption. `astra --dry-run clean` lists abandoned
ASTRA links; `astra --yes clean` removes only those links. All recorded
generations and their roots are retained.

For a known good previous ASTRA generation, inspect `astra history show ID`,
then use `astra rollback ID` and confirm the plan. Rollback creates another
auditable profile transaction. It restores package files in the managed
profile, not application data, system configuration, service databases, or a
whole OS snapshot.

If the schema migration backup is incomplete, a journal is corrupt, the
profile points to an unknown store path, or Nix reports signature failure,
stop. Copy the evidence and seek an operator review. Do not delete journals,
GC roots, or SQLite state to force a new transaction.
