#!/bin/sh
set -eu
if [ "$1" = '--version' ]; then
    echo 'nix (Nix) 2.28.0'
elif [ "$1" = '--extra-experimental-features' ]; then
    shift 2
    if [ "$1" = config ] && [ "$2" = show ]; then
        cat "${ASTRA_MOCK_CONFIG_FILE:-$ASTRA_FIXTURE_DIR/nix-config.json}"
        exit 0
    fi
    while [ "$1" = --option ]; do
        case "$2:$3" in
            'require-sigs:true'|'trusted-public-keys:cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY='|'substituters:https://cache.nixos.org/') ;;
            *) exit 4 ;;
        esac
        shift 3
    done
    if [ "$1" = flake ] && [ "$2" = metadata ]; then
        if [ "${ASTRA_MOCK_FAIL_METADATA:-0}" = 1 ]; then
            echo '{"locked":{"type":"github","owner":"attacker","repo":"nixpkgs","rev":"0123456789abcdef0123456789abcdef01234567","narHash":"sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0="}}'
        else
            cat "${ASTRA_MOCK_METADATA_FILE:-$ASTRA_FIXTURE_DIR/nix-metadata.json}"
        fi
    elif [ "$1" = search ]; then
        cat "$ASTRA_FIXTURE_DIR/nix-search.json"
    else
        exit 4
    fi
else
    exit 4
fi
