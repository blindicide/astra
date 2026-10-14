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
"$ASTRA_BIN" repo update >/dev/null
"$ASTRA_BIN" --yes install hello jq > "$TEST_DIR/install.out"
grep -F 'committed' "$TEST_DIR/install.out"
"$ASTRA_BIN" list > "$TEST_DIR/list.out"
grep -F 'hello' "$TEST_DIR/list.out"
grep -F 'jq' "$TEST_DIR/list.out"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" = 1
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM package_selections;")" = 2
"$ASTRA_BIN" --yes remove jq > "$TEST_DIR/remove.out"
grep -F 'committed' "$TEST_DIR/remove.out"
"$ASTRA_BIN" list | grep -F hello
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" = 2
ASTRA_TEST_FAULT_POINT=after_journal "$ASTRA_BIN" --yes install jq > "$TEST_DIR/fault.out" 2>&1 && exit 1
"$ASTRA_BIN" history > "$TEST_DIR/history.out"
grep -F 'not_applied' "$TEST_DIR/history.out"
ASTRA_TEST_FAULT_POINT=after_activation "$ASTRA_BIN" --yes install jq > "$TEST_DIR/fault2.out" 2>&1 && exit 1
"$ASTRA_BIN" history > "$TEST_DIR/history2.out"
grep -F 'committed' "$TEST_DIR/history2.out"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" = 3
"$ASTRA_BIN" --yes rollback > "$TEST_DIR/rollback.out"
grep -F 'committed' "$TEST_DIR/rollback.out"
"$ASTRA_BIN" list > "$TEST_DIR/rolled.list"
grep -F hello "$TEST_DIR/rolled.list"
if grep -F jq "$TEST_DIR/rolled.list"; then exit 1; fi
first_hash=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT hash FROM transactions WHERE sequence=1;')
first_id=$(printf %s "$first_hash" | tail -c 9)
"$ASTRA_BIN" --offline --yes rollback "$first_id" > "$TEST_DIR/rollback-explicit.out"
grep -F 'committed' "$TEST_DIR/rollback-explicit.out"
"$ASTRA_BIN" list | grep -F jq
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" = 5
for hash in $(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT hash FROM transactions;'); do
    intent=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT intent FROM transactions WHERE hash='$hash';")
    test "$(printf %s "$intent" | sha256sum | cut -d ' ' -f1)" = "$hash"
    test "$(cat "$XDG_STATE_HOME/astra/profiles/default/journals/$hash.json")" = "$intent"
done
if sqlite3 "$XDG_STATE_HOME/astra/state.db" "UPDATE transactions SET intent='changed' WHERE sequence=1;" > "$TEST_DIR/immutable.out" 2>&1; then exit 1; fi
grep -F 'transaction intent is immutable' "$TEST_DIR/immutable.out"
"$ASTRA_BIN" --profile second --yes install jq > "$TEST_DIR/second.out"
"$ASTRA_BIN" --profile second list | grep -F jq
"$ASTRA_BIN" list | grep -F hello
test -f "$XDG_STATE_HOME/astra/state.db.backup-v1"
