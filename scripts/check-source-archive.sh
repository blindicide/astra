#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo 'usage: check-source-archive.sh ARCHIVE.tar.zst' >&2
    exit 2
fi
archive=$(realpath "$1")
test -f "$archive" || { echo 'source archive is missing' >&2; exit 2; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
tar --zstd -xf "$archive" -C "$work"
set -- "$work"/*
test "$#" -eq 1 && test -d "$1" || { echo 'archive must contain one source directory' >&2; exit 2; }
cd "$1"
make -j2
make check
make DESTDIR="$work/stage" PREFIX=/usr/local install
test -x "$work/stage/usr/local/bin/astra"
"$work/stage/usr/local/bin/astra" --version
if ! command -v nix >/dev/null 2>&1; then
    set +e
    XDG_STATE_HOME="$work/state" "$work/stage/usr/local/bin/astra" doctor > "$work/doctor.out" 2>&1
    doctor_status=$?
    set -e
    test "$doctor_status" -eq 1 || { echo 'doctor did not report the missing Nix backend' >&2; exit 1; }
    grep -F 'Nix executable is missing' "$work/doctor.out"
else
    XDG_STATE_HOME="$work/state" "$work/stage/usr/local/bin/astra" doctor > "$work/doctor.out" 2>&1 || true
    grep -F 'Database: ok' "$work/doctor.out"
fi
