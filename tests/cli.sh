#!/bin/sh
set -eu
ASTRA_BIN=$1
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT HUP INT TERM
export XDG_STATE_HOME="$TEST_DIR/state"
"$ASTRA_BIN" --version | grep -Fx 'astra 0.2.0-dev'
"$ASTRA_BIN" help | grep -F 'Usage: astra'
set +e
"$ASTRA_BIN" --json doctor > "$TEST_DIR/doctor.json"
DOCTOR_STATUS=$?
set -e
test "$DOCTOR_STATUS" -eq 1
grep -F '"schema_version":1' "$TEST_DIR/doctor.json"
grep -F '"found":false' "$TEST_DIR/doctor.json"
test -f "$XDG_STATE_HOME/astra/state.db"
test "$(stat -c '%a' "$XDG_STATE_HOME/astra")" = 700
test "$(stat -c '%a' "$XDG_STATE_HOME/astra/state.db")" = 600
sqlite3 "$XDG_STATE_HOME/astra/state.db" 'pragma integrity_check; select max(version) from schema_versions;' | grep -Fx 'ok'
if "$ASTRA_BIN" install hello > "$TEST_DIR/unavailable.out" 2>&1; then exit 1; fi
grep -F 'unavailable' "$TEST_DIR/unavailable.out"
