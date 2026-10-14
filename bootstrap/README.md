# Nix bootstrap candidate

`preflight.sh LOG-PATH` records read-only architecture, libc, systemd, store,
and Nix version diagnostics in a new private log. It deliberately returns
failure until a version-pinned Nix runtime and all of its target-compatible
dependencies have been selected and tested on Almagest-G. It does not create
`/nix`, users, systemd units, or trust settings.

A complete kit must carry a payload manifest with version, source URL,
cryptographic digest, licensing, expected interpreter and libc versions, and
verified package contents. Target testing must cover persistent store
permissions, build users, daemon/socket startup, signature verification,
substitution, reboot persistence, and removal/recovery. A checksum from the
same untrusted download channel is insufficient publisher authentication.

No runtime payload or bootstrap archive is distributed from this branch.
