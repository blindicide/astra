# Installing a development build

Build and check from a complete source tree as described in [BUILDING.md](BUILDING.md).
For a staged installation, run
`make DESTDIR=/temporary/staging PREFIX=/usr/local install` and inspect
`/temporary/staging/usr/local/bin/astra`. On an approved target, the operator
may then run `make PREFIX=/usr/local install` with the required filesystem
privileges. The install target copies one executable and does not configure
Nix, shell startup, services, or system profiles.

`astra --version` works without Nix. `astra doctor` reports a missing Nix
backend. Package operations require a supported Nix runtime with correct
daemon and cache trust configuration; the version check alone is insufficient.
The development host has no Nix, and the separate Almagest-G bootstrap kit
is incomplete. See [bootstrap/README.md](bootstrap/README.md).

For a per-user profile, first run `astra update`, then review
`astra install PACKAGE` before confirming. Add
`$XDG_STATE_HOME/astra/profiles/default/profile/bin` to `PATH` explicitly
if desired. When `XDG_STATE_HOME` is unset, use
`$HOME/.local/state/astra/profiles/default/profile/bin`. ASTRA does not
change existing Nix profiles or shell initialization.

To upgrade the ASTRA executable later, preserve the private state directory,
build the new source in a separate tree, run its tests, stage the executable,
and inspect `astra doctor` before replacing the previous executable. Schema
migration creates a backup; see [RECOVERY.md](RECOVERY.md) before restoring
older code against newer state.
