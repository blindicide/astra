#!/bin/sh
set -eu
ASTRA_BIN=$(realpath "$1")
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT HUP INT TERM

make_v1() {
    mkdir -p "$1/astra"
    chmod 700 "$1/astra"
    sqlite3 "$1/astra/state.db" 'CREATE TABLE schema_versions(version INTEGER PRIMARY KEY); INSERT INTO schema_versions(version) VALUES(1);'
    chmod 600 "$1/astra/state.db"
}

make_v1 "$TEST_DIR/valid"
export XDG_STATE_HOME="$TEST_DIR/valid"
"$ASTRA_BIN" doctor > /dev/null 2>&1 && exit 1
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT max(version) FROM schema_versions;')" = 2
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db.backup-v1" 'SELECT max(version) FROM schema_versions;')" = 1
test "$(stat -c '%a' "$XDG_STATE_HOME/astra/state.db.backup-v1")" = 600

make_v1 "$TEST_DIR/incomplete"
export XDG_STATE_HOME="$TEST_DIR/incomplete"
: > "$XDG_STATE_HOME/astra/state.db.backup-v1"
chmod 600 "$XDG_STATE_HOME/astra/state.db.backup-v1"
"$ASTRA_BIN" doctor > "$TEST_DIR/incomplete.err" 2>&1 && exit 1
grep -F 'database migration backup is incomplete or corrupt' "$TEST_DIR/incomplete.err"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT max(version) FROM schema_versions;')" = 1

make_v1 "$TEST_DIR/symlink"
export XDG_STATE_HOME="$TEST_DIR/symlink"
printf '%s' 'sentinel' > "$TEST_DIR/sentinel"
ln -s "$TEST_DIR/sentinel" "$XDG_STATE_HOME/astra/state.db.backup-v1"
"$ASTRA_BIN" doctor > "$TEST_DIR/symlink.err" 2>&1 && exit 1
grep -F 'ASTRA migration backup path is unsafe' "$TEST_DIR/symlink.err"
test "$(cat "$TEST_DIR/sentinel")" = sentinel

make_v1 "$TEST_DIR/retry"
export XDG_STATE_HOME="$TEST_DIR/retry"
cp "$XDG_STATE_HOME/astra/state.db" "$XDG_STATE_HOME/astra/state.db.backup-v1"
"$ASTRA_BIN" doctor > /dev/null 2>&1 && exit 1
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT max(version) FROM schema_versions;')" = 2
