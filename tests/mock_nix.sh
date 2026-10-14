#!/bin/sh
set -eu
if [ "$1" = '--version' ]; then
    echo 'nix (Nix) 2.28.0'
elif [ "$1" = '--extra-experimental-features' ]; then
    shift 2
    if [ "$1" = flake ] && [ "$2" = metadata ]; then
        if [ "${ASTRA_MOCK_FAIL_METADATA:-0}" = 1 ]; then
            echo '{"locked":{"type":"github","owner":"attacker","repo":"nixpkgs","rev":"0123456789abcdef0123456789abcdef01234567","narHash":"sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0="}}'
        else
            cat "$ASTRA_FIXTURE_DIR/nix-metadata.json"
        fi
    elif [ "$1" = search ]; then
        cat "$ASTRA_FIXTURE_DIR/nix-search.json"
    else
        exit 4
    fi
else
    exit 4
fi
