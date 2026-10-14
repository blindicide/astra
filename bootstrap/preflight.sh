#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo 'usage: preflight.sh DIAGNOSTIC-LOG' >&2
    exit 2
fi
log=$1
if [ -e "$log" ] || [ -L "$log" ]; then
    echo 'diagnostic log already exists; choose a new path' >&2
    exit 2
fi
umask 077
set -C
exec > "$log" 2>&1
echo 'ASTRA Nix bootstrap preflight (read-only)'
date -u '+UTC: %Y-%m-%dT%H:%M:%SZ'
echo "Kernel: $(uname -s)"
echo "Architecture: $(uname -m)"
echo "Word size: $(getconf LONG_BIT 2>/dev/null || echo unknown)"
echo "libc: $(getconf GNU_LIBC_VERSION 2>/dev/null || echo unknown)"
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    echo 'STOP: this bootstrap candidate only targets x86_64 Linux'
    exit 1
fi
if ! command -v systemctl >/dev/null 2>&1; then
    echo 'STOP: systemd tools are unavailable; daemon setup has not been validated'
    exit 1
fi
if [ ! -d /nix/store ]; then
    echo 'Nix store is absent; no storage or users will be created by this preflight'
fi
if command -v nix >/dev/null 2>&1; then
    nix --version
else
    echo 'Nix runtime is absent'
fi
echo 'STOP: no version-pinned, target-tested Nix runtime payload is supplied'
exit 1
