# Security and trust

ASTRA is an unprivileged per-user application. It is not setuid and does not
change the kernel, bootloader, systemd, glibc, PAM, or arbitrary services.
State, journals, and locks live in a private user directory. The transaction
engine checks profile links, immutable intent hashes, journal bytes, and
recorded GC roots before mutations. Unexpected state stops further changes.

Repository updates accept only the configured `NixOS/nixpkgs` GitHub source
with an immutable 40-character revision and NAR hash. ASTRA currently accepts
only the official `https://cache.nixos.org/` substituter and its pinned public
key. It rejects effective Nix configurations with disabled signature checks,
different trusted keys, or other substituters. Nix CLI operations also set
these values explicitly. Malformed configuration JSON, including trailing
non-whitespace content, is rejected. A cache miss can lead to a source build
only after `--allow-build`; `--yes` alone is not build consent. A failed
signature copy aborts even when building was permitted. `--offline` does not
probe the cache.

No third-party cache or overlay approval mechanism exists yet. Adding one
would require an explicit source and key review, provenance records, and
rejection tests. Nix daemon policy remains authoritative. The tests use a
fake backend and cannot prove live signature enforcement; do not treat this
branch as certified for untrusted binary inputs.

Report vulnerabilities through the repository's private security reporting
channel if one is configured, or contact the maintainers privately. Do not
include private keys or credentials in issue reports or source archives.
