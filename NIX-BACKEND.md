# Nix backend compatibility

ASTRA calls Nix with explicit argument arrays and no shell. Candidate CLI versions are Nix 2.18 through 2.30; this range is an initial parser policy, not a verified compatibility matrix. `doctor` currently checks the version string and SQLite integrity. Package operations require additional capability checks and a working Nix daemon, which have not been implemented or validated.

`repo update` uses `nix flake metadata --json` and `nix search --json` with `nix-command flakes` experimental features. Nix's own store, evaluator, sandbox, daemon permissions, and signature/trust policy remain authoritative. ASTRA does not alter Nix cache trust settings.

The development host has no `nix` executable. Tests use a disposable fake executable to exercise parsing and refusal behavior; those tests are not evidence of an operational Nix backend.
