#!/bin/sh
set -eu
ASTRA_BIN=$1
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT HUP INT TERM
export XDG_STATE_HOME="$TEST_DIR/state"
"$ASTRA_BIN" --version | grep -Fx 'astra 0.3.0-dev'
"$ASTRA_BIN" help | grep -F 'Usage: astra'
"$ASTRA_BIN" install --help | grep -F 'Usage: astra'
if "$ASTRA_BIN" --quiet list > "$TEST_DIR/quiet-invalid.out" 2>&1; then exit 1; fi
grep -F -- '--quiet is only meaningful' "$TEST_DIR/quiet-invalid.out"
if "$ASTRA_BIN" --json --verbose doctor > "$TEST_DIR/output-conflict.out" 2>&1; then exit 1; fi
grep -F 'mutually exclusive' "$TEST_DIR/output-conflict.out"
if "$ASTRA_BIN" --profile default doctor > "$TEST_DIR/profile-invalid.out" 2>&1; then exit 1; fi
grep -F -- '--profile is only meaningful' "$TEST_DIR/profile-invalid.out"
if "$ASTRA_BIN" --dry-run list > "$TEST_DIR/dry-run-invalid.out" 2>&1; then exit 1; fi
grep -F -- '--yes and --dry-run are only meaningful' "$TEST_DIR/dry-run-invalid.out"
if "$ASTRA_BIN" --unknown doctor > "$TEST_DIR/unknown.out" 2>&1; then exit 1; fi
grep -F 'unknown switch: --unknown' "$TEST_DIR/unknown.out"
if "$ASTRA_BIN" --quiet --version > "$TEST_DIR/version-invalid.out" 2>&1; then exit 1; fi
grep -F -- '--quiet is only meaningful' "$TEST_DIR/version-invalid.out"
"$ASTRA_BIN" --verbose --json help > "$TEST_DIR/help-conflict.out" 2>&1 && exit 1
grep -F 'mutually exclusive' "$TEST_DIR/help-conflict.out"
set +e
"$ASTRA_BIN" --verbose doctor > "$TEST_DIR/verbose.out" 2>&1
VERBOSE_STATUS=$?
set -e
test "$VERBOSE_STATUS" -eq 1
grep -F 'ASTRA state directory:' "$TEST_DIR/verbose.out"
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
grep -F 'supported Nix backend' "$TEST_DIR/unavailable.out"
