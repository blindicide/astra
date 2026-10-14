#!/bin/sh
set -eu
case "${0##*/}" in
    nix)
        if [ "$1" = --version ]; then echo 'nix (Nix) 2.28.0'; exit 0; fi
        test "$1" = --extra-experimental-features
        shift 2
        case "$1 $2" in
            'flake metadata') cat "$ASTRA_FIXTURE_DIR/nix-metadata.json" ;;
            'search --json') cat "$ASTRA_FIXTURE_DIR/nix-search.json" ;;
            'eval --raw') echo x86_64-linux ;;
            'build --dry-run') echo 'these derivations may be built locally' >&2 ;;
            'build --no-link')
                if [ "${ASTRA_MOCK_FAIL_BUILD:-0}" = 1 ]; then echo 'mock build failure' >&2; exit 5; fi
                case "$4" in
                    *'#hello') echo '/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-hello' ;;
                    *'#jq') echo '/nix/store/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb-jq' ;;
                    *'#emptyDirectory') echo '/nix/store/cccccccccccccccccccccccccccccccc-empty' ;;
                    *) exit 4 ;;
                esac ;;
            'profile install')
                if [ "${ASTRA_MOCK_FAIL_STAGE:-0}" = 1 ]; then echo 'mock stage failure' >&2; exit 5; fi
                test "$3" = --profile
                stage=$4
                shift 4
                digest=$(printf '%s\n' "$@" | sha256sum | cut -c1-32)
                ln -s "/nix/store/$digest-profile" "$stage" ;;
            *) exit 4 ;;
        esac ;;
    nix-store)
        case "$1" in
            --verify-path) test "${2#/nix/store/}" != "$2" ;;
            --add-root) test "${ASTRA_MOCK_FAIL_ROOT:-0}" != 1; test "$3" = --realise; ln -s "$4" "$2" ;;
            *) exit 4 ;;
        esac ;;
    nix-env)
        if [ "${ASTRA_MOCK_FAIL_ACTIVATION:-0}" = 1 ]; then echo 'mock activation failure' >&2; exit 5; fi
        test "$1" = --profile && test "$3" = --set
        ln -s "$4" "$2.new"
        mv -Tf "$2.new" "$2" ;;
    *) exit 4 ;;
esac
