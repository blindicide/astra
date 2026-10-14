#!/bin/sh
set -eu
case "${0##*/}" in
    nix)
        if [ "$1" = --version ]; then echo 'nix (Nix) 2.28.0'; exit 0; fi
        offline=0
        if [ "$1" = --offline ]; then offline=1; shift; fi
        test "$1" = --extra-experimental-features
        shift 2
        if [ "$1" = config ] && [ "$2" = show ]; then
            cat "${ASTRA_MOCK_CONFIG_FILE:-$ASTRA_FIXTURE_DIR/nix-config.json}"
            exit 0
        fi
        if [ "${ASTRA_MOCK_REQUIRE_OFFLINE:-0}" = 1 ]; then test "$offline" = 1; fi
        while [ "$1" = --option ]; do
            case "$2:$3" in
                'require-sigs:true'|'trusted-public-keys:cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY='|'substituters:https://cache.nixos.org/') ;;
                *) exit 4 ;;
            esac
            shift 3
        done
        case "$1 $2" in
            'flake metadata') cat "$ASTRA_FIXTURE_DIR/nix-metadata.json" ;;
            'search --json') cat "$ASTRA_FIXTURE_DIR/nix-search.json" ;;
            'eval --raw')
                if [ "$3" = --impure ]; then echo x86_64-linux; exit 0; fi
                case "$3" in
                    *1111111111111111111111111111111111111111'#hello') echo '/nix/store/dddddddddddddddddddddddddddddddd-hello' ;;
                    *1111111111111111111111111111111111111111'#jq') echo '/nix/store/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee-jq' ;;
                    *'#hello') echo '/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-hello' ;;
                    *'#jq') echo '/nix/store/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb-jq' ;;
                    *'#emptyDirectory') echo '/nix/store/cccccccccccccccccccccccccccccccc-empty' ;;
                    *) exit 4 ;;
                esac ;;
            'build --dry-run') echo 'these derivations may be built locally' >&2 ;;
            'build --no-link')
                if [ "${ASTRA_MOCK_FAIL_BUILD:-0}" = 1 ]; then echo 'mock build failure' >&2; exit 5; fi
                case "$4" in
                    *1111111111111111111111111111111111111111'#hello') echo '/nix/store/dddddddddddddddddddddddddddddddd-hello' ;;
                    *1111111111111111111111111111111111111111'#jq') echo '/nix/store/eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee-jq' ;;
                    *'#hello')
                        if [ -n "${ASTRA_MOCK_COPIED_DIR:-}" ]; then touch "$ASTRA_MOCK_COPIED_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-hello"; fi
                        echo '/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-hello' ;;
                    *'#jq') echo '/nix/store/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb-jq' ;;
                    *'#emptyDirectory') echo '/nix/store/cccccccccccccccccccccccccccccccc-empty' ;;
                    *) exit 4 ;;
                esac ;;
            'path-info --store')
                test "${ASTRA_MOCK_FAIL_CACHE_TOUCH:-0}" != 1
                test "$3" = 'https://cache.nixos.org/'
                test "${ASTRA_MOCK_CACHE_AVAILABLE:-0}" = 1
                printf '%s\n' "$4" ;;
            'copy --from')
                test "${ASTRA_MOCK_FAIL_CACHE_TOUCH:-0}" != 1
                test "$3" = 'https://cache.nixos.org/'
                if [ "${ASTRA_MOCK_BAD_SIGNATURE:-0}" = 1 ]; then echo 'signature rejected' >&2; exit 5; fi
                test "${ASTRA_MOCK_CACHE_AVAILABLE:-0}" = 1
                test -n "${ASTRA_MOCK_COPIED_DIR:-}"
                touch "$ASTRA_MOCK_COPIED_DIR/${4##*/}" ;;
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
            --verify-path)
                test "${2#/nix/store/}" != "$2"
                if [ "${ASTRA_MOCK_MISSING_STORE:-0}" = 1 ]; then
                    case "$2" in *-hello|*-jq)
                        if [ -z "${ASTRA_MOCK_COPIED_DIR:-}" ] || [ ! -f "$ASTRA_MOCK_COPIED_DIR/${2##*/}" ]; then exit 5; fi ;;
                    esac
                fi ;;
            --query)
                test "$2" = --requisites
                case "$3" in
                    *-hello|*-jq) printf '%s\n' "$3" '/nix/store/ffffffffffffffffffffffffffffffff-sharedlib-1.0' ;;
                    *) printf '%s\n' "$3" ;;
                esac ;;
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
