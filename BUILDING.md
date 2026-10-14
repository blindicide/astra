# Building ASTRA

Prerequisites for this development build: C++20 compiler (tested with GCC 13.3), C compiler, GNU Make, POSIX/Linux libc, pthread/dl libraries, and json-c development files (tested with 0.17). SQLite 3.46.1 source is included. Nix is not needed to compile, run `--version`, or run `doctor`.

```sh
make -j2
make check
make PREFIX=/usr/local DESTDIR=/temporary/staging install
```

`make install` copies only the `astra` executable. The build has no package manager calls and no network operations. State defaults to `$XDG_STATE_HOME/astra/state.db` or `$HOME/.local/state/astra/state.db`; ASTRA creates its own directory with mode 0700 and database with mode 0600. `doctor` exits nonzero if Nix is absent or state integrity fails.

The Almagest-G local GCC/toolchain and runtime prerequisites have not been inspected or validated. Nix bootstrap is a separate future deliverable; offline ASTRA compilation does not imply offline Nix installation.
