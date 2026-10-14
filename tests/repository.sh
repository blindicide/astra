#!/bin/sh
set -eu
ASTRA_BIN=$(realpath "$1")
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT HUP INT TERM
mkdir -p "$TEST_DIR/bin"
cp tests/mock_nix.sh "$TEST_DIR/bin/nix"
chmod 755 "$TEST_DIR/bin/nix"
export PATH="$TEST_DIR/bin:$PATH"
export XDG_STATE_HOME="$TEST_DIR/state"
export ASTRA_FIXTURE_DIR=$(realpath tests/fixtures)
"$ASTRA_BIN" --json repo list | grep -F '"repositories":[]'
"$ASTRA_BIN" --offline repo update > "$TEST_DIR/offline.out" 2>&1 && exit 1
grep -F 'unavailable with --offline' "$TEST_DIR/offline.out"
"$ASTRA_BIN" --offline update > "$TEST_DIR/offline-update.out" 2>&1 && exit 1
grep -F 'unavailable with --offline' "$TEST_DIR/offline-update.out"
"$ASTRA_BIN" --json repo update > "$TEST_DIR/update.json"
grep -F '"indexed_packages":2' "$TEST_DIR/update.json"
grep -F '"nar_hash":"sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0="' "$TEST_DIR/update.json"
"$ASTRA_BIN" --json update > "$TEST_DIR/update-alias.json"
grep -F '"command":"update"' "$TEST_DIR/update-alias.json"
grep -F '"profiles_changed":false' "$TEST_DIR/update-alias.json"
"$ASTRA_BIN" --quiet update > "$TEST_DIR/quiet-update.out"
test ! -s "$TEST_DIR/quiet-update.out"
"$ASTRA_BIN" --offline --json search hello > "$TEST_DIR/search.json"
grep -F '"name":"hello"' "$TEST_DIR/search.json"
"$ASTRA_BIN" --json info legacyPackages.x86_64-linux.jq > "$TEST_DIR/info.json"
grep -F '"availability":"unverified"' "$TEST_DIR/info.json"
"$ASTRA_BIN" --json repo list | grep -F '0123456789abcdef0123456789abcdef01234567'
"$ASTRA_BIN" --offline --json repo list | grep -F '"nar_hash":"sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0="'
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT nar_hash FROM repository_revisions;')" = 'sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0='
sed 's@sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0=@sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=@' tests/fixtures/nix-metadata.json > "$TEST_DIR/conflicting-metadata.json"
ASTRA_MOCK_METADATA_FILE="$TEST_DIR/conflicting-metadata.json" "$ASTRA_BIN" update > "$TEST_DIR/conflict.out" 2>&1 && exit 1
grep -F 'repository content hash differs from the recorded revision' "$TEST_DIR/conflict.out"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT nar_hash FROM repository_revisions;')" = 'sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0='
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT count(*) FROM packages;')" = 2
sqlite3 "$XDG_STATE_HOME/astra/state.db" "UPDATE repository_revisions SET nar_hash='sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=';" > "$TEST_DIR/immutable.out" 2>&1 && exit 1
grep -F 'repository content hash is immutable' "$TEST_DIR/immutable.out"
export ASTRA_MOCK_FAIL_METADATA=1
"$ASTRA_BIN" repo update > "$TEST_DIR/failure.out" 2>&1 && exit 1
grep -F 'unexpected repository identity' "$TEST_DIR/failure.out"
"$ASTRA_BIN" --json repo list | grep -F '0123456789abcdef0123456789abcdef01234567'
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'select count(*) from packages;')" = 2
unset ASTRA_MOCK_FAIL_METADATA
for fault in unsigned key substituter malformed trailing; do
    case "$fault" in
        unsigned) sed 's/"true"/"false"/' tests/fixtures/nix-config.json > "$TEST_DIR/config.json" ;;
        key) sed 's/cache.nixos.org-1:/attacker-1:/' tests/fixtures/nix-config.json > "$TEST_DIR/config.json" ;;
        substituter) sed 's@https://cache.nixos.org/@https://attacker.example/@' tests/fixtures/nix-config.json > "$TEST_DIR/config.json" ;;
        malformed) printf '%s\n' '{broken' > "$TEST_DIR/config.json" ;;
        trailing) cat tests/fixtures/nix-config.json > "$TEST_DIR/config.json"; printf '%s\n' 'untrusted-data' >> "$TEST_DIR/config.json" ;;
    esac
    ASTRA_MOCK_CONFIG_FILE="$TEST_DIR/config.json" "$ASTRA_BIN" update > "$TEST_DIR/$fault.out" 2>&1 && exit 1
    grep -E 'cache trust|cache policy' "$TEST_DIR/$fault.out"
    test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'select count(*) from packages;')" = 2
done
