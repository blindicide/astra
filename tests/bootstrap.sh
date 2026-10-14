#!/bin/sh
set -eu
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
set +e
sh bootstrap/preflight.sh "$work/diagnostic.log"
status=$?
set -e
test "$status" -eq 1
test "$(stat -c '%a' "$work/diagnostic.log")" = 600
grep -F 'ASTRA Nix bootstrap preflight' "$work/diagnostic.log"
grep -E 'STOP:|target-tested Nix runtime' "$work/diagnostic.log"
if sh bootstrap/preflight.sh "$work/diagnostic.log" > "$work/existing.out" 2>&1; then exit 1; fi
grep -F 'already exists' "$work/existing.out"
ln -s "$work/missing-target" "$work/symlink.log"
if sh bootstrap/preflight.sh "$work/symlink.log" > "$work/symlink.out" 2>&1; then exit 1; fi
grep -F 'already exists' "$work/symlink.out"
test ! -e "$work/missing-target"
