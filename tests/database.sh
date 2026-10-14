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
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT max(version) FROM schema_versions;')" = 3
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db.backup-v1" 'SELECT max(version) FROM schema_versions;')" = 1
test "$(stat -c '%a' "$XDG_STATE_HOME/astra/state.db.backup-v1")" = 600
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db.backup-v2" 'SELECT max(version) FROM schema_versions;')" = 2
test "$(stat -c '%a' "$XDG_STATE_HOME/astra/state.db.backup-v2")" = 600

mkdir -p "$TEST_DIR/v2-history/astra"
chmod 700 "$TEST_DIR/v2-history/astra"
cp "$XDG_STATE_HOME/astra/state.db.backup-v2" "$TEST_DIR/v2-history/astra/state.db"
sqlite3 "$TEST_DIR/v2-history/astra/state.db" "INSERT INTO repositories(name,url,selected_revision) VALUES('nixpkgs','github:NixOS/nixpkgs/nixos-26.05','0123456789abcdef0123456789abcdef01234567'); INSERT INTO repository_revisions(repository_id,revision) VALUES(1,'0123456789abcdef0123456789abcdef01234567');"
export XDG_STATE_HOME="$TEST_DIR/v2-history"
"$ASTRA_BIN" doctor > /dev/null 2>&1 && exit 1
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT max(version) FROM schema_versions;')" = 3
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT coalesce(nar_hash, "absent") FROM repository_revisions;')" = absent
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db.backup-v2" 'SELECT count(*) FROM repository_revisions;')" = 1

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
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT max(version) FROM schema_versions;')" = 3

make_v1 "$TEST_DIR/v2-incomplete"
export XDG_STATE_HOME="$TEST_DIR/v2-incomplete"
"$ASTRA_BIN" doctor > /dev/null 2>&1 && exit 1
sqlite3 "$XDG_STATE_HOME/astra/state.db" 'DELETE FROM schema_versions WHERE version=3; DROP TRIGGER immutable_revision_hash; ALTER TABLE repository_revisions DROP COLUMN nar_hash;'
: > "$XDG_STATE_HOME/astra/state.db.backup-v2"
chmod 600 "$XDG_STATE_HOME/astra/state.db.backup-v2"
"$ASTRA_BIN" doctor > "$TEST_DIR/v2-incomplete.err" 2>&1 && exit 1
grep -F 'database migration backup is incomplete or corrupt' "$TEST_DIR/v2-incomplete.err"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT max(version) FROM schema_versions;')" = 2
