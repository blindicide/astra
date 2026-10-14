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
"$ASTRA_BIN" --json update > "$TEST_DIR/update-alias.json"
grep -F '"command":"update"' "$TEST_DIR/update-alias.json"
grep -F '"profiles_changed":false' "$TEST_DIR/update-alias.json"
"$ASTRA_BIN" --offline --json search hello > "$TEST_DIR/search.json"
grep -F '"name":"hello"' "$TEST_DIR/search.json"
"$ASTRA_BIN" --json info legacyPackages.x86_64-linux.jq > "$TEST_DIR/info.json"
grep -F '"availability":"unverified"' "$TEST_DIR/info.json"
"$ASTRA_BIN" --json repo list | grep -F '0123456789abcdef0123456789abcdef01234567'
export ASTRA_MOCK_FAIL_METADATA=1
"$ASTRA_BIN" repo update > "$TEST_DIR/failure.out" 2>&1 && exit 1
grep -F 'unexpected repository identity' "$TEST_DIR/failure.out"
"$ASTRA_BIN" --json repo list | grep -F '0123456789abcdef0123456789abcdef01234567'
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'select count(*) from packages;')" = 2
