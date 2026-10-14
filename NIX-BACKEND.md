# Nix backend compatibility

ASTRA calls Nix with explicit argument arrays and no shell. Candidate CLI versions are Nix 2.18 through 2.30; this range is an initial parser policy, not a verified compatibility matrix. `doctor` currently checks the version string and SQLite integrity. Profile operations also invoke `nix eval`, `nix build`, `nix profile install`, `nix-store`, and `nix-env`; a version string alone does not validate these capabilities or daemon access.

`repo update` uses `nix flake metadata --json` and `nix search --json` with `nix-command flakes` experimental features. Nix's own store, evaluator, sandbox, daemon permissions, and signature/trust policy remain authoritative. ASTRA does not alter Nix cache trust settings.

The development host has no `nix` executable. Tests use a disposable fake executable to exercise parsing and refusal behavior; those tests are not evidence of an operational Nix backend.

The profile strategy follows documented Nix operations: [`nix profile install --profile`](https://nix.dev/manual/nix/2.18/command-ref/new-cli/nix3-profile-install.html) builds a complete staged user environment, [`nix-env --profile --set`](https://nix.dev/manual/nix/2.23/command-ref/nix-env/set) switches the visible managed profile, and [`nix-store --add-root --realise`](https://nix.dev/manual/nix/2.28/command-ref/nix-store/realise) protects retained store paths. The newer `nix profile` interface is documented as experimental. These references explain the intended mechanism; live compatibility and crash durability remain to be tested on a real Nix installation.
