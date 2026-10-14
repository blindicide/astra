#!/bin/sh
set -eu
ASTRA_BIN=$(realpath "$1")
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT HUP INT TERM
mkdir -p "$TEST_DIR/bin"
cp tests/mock_profile_backend.sh "$TEST_DIR/bin/nix"
chmod 755 "$TEST_DIR/bin/nix"
ln -s nix "$TEST_DIR/bin/nix-store"
ln -s nix "$TEST_DIR/bin/nix-env"
export PATH="$TEST_DIR/bin:$PATH"
export XDG_STATE_HOME="$TEST_DIR/state"
export ASTRA_FIXTURE_DIR=$(realpath tests/fixtures)

"$ASTRA_BIN" --json doctor > "$TEST_DIR/healthy.json"
python3 -m json.tool "$TEST_DIR/healthy.json" >/dev/null
grep -F '"trusted":true' "$TEST_DIR/healthy.json" >/dev/null
grep -F '"reachable":true' "$TEST_DIR/healthy.json" >/dev/null
ASTRA_MOCK_FAIL_DAEMON=1 "$ASTRA_BIN" --json doctor > "$TEST_DIR/daemon.json" && exit 1
grep -F '"detail":"Nix daemon is unreachable"' "$TEST_DIR/daemon.json" >/dev/null
ASTRA_MOCK_FAIL_CACHE_PING=1 "$ASTRA_BIN" --json doctor > "$TEST_DIR/cache.json" && exit 1
grep -F '"detail":"official cache is unreachable"' "$TEST_DIR/cache.json" >/dev/null
ASTRA_MOCK_FAIL_CACHE_TOUCH=1 "$ASTRA_BIN" --offline --json doctor > "$TEST_DIR/offline.json"
grep -F '"detail":"skipped offline"' "$TEST_DIR/offline.json" >/dev/null
"$ASTRA_BIN" --offline --json verify > "$TEST_DIR/verify.json"
grep -F '"command":"verify"' "$TEST_DIR/verify.json" >/dev/null
chmod 755 "$XDG_STATE_HOME/astra"
"$ASTRA_BIN" doctor > "$TEST_DIR/permissions.out" 2>&1 && exit 1
grep -F 'unsafe ownership or permissions' "$TEST_DIR/permissions.out" >/dev/null
