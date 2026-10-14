# Architecture

The `astra` C++20 executable owns CLI policy, a private SQLite database,
repository indexing, per-user profile intent, and recovery. Nix owns flake
evaluation, package outputs, the immutable store, binary substitution,
profile environment construction, and GC references. ASTRA passes structured
argument arrays to Nix and never evaluates package metadata as shell text.

`RepositoryManager` resolves the approved nixpkgs update source to an
immutable Git revision and NAR hash, indexes the revision, and atomically
selects it. `TransactionEngine` locks one named user profile, prepares a
complete generation, writes a canonical intent and separate outcome events,
activates one profile link, and reconciles the database after interruption.
`Database` owns schema migrations, backups, and integrity checks.

The current integration is exercised with a deterministic fake Nix backend.
The version range 2.18–2.30 is a compatibility candidate and is not a
live-tested support matrix. A system-wide applications profile, overlays,
live Nix integration, and Almagest-G validation remain open.
