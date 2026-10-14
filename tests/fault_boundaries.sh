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
export ASTRA_FIXTURE_DIR=$(realpath tests/fixtures)
for point in after_build after_staging after_root before_prepared_db after_prepared_db before_journal after_journal before_activation after_activation after_verification before_commit after_commit; do
    export XDG_STATE_HOME="$TEST_DIR/$point"
    "$ASTRA_BIN" repo update >/dev/null
    "$ASTRA_BIN" --yes install hello >/dev/null
    set +e
    ASTRA_TEST_FAULT_POINT="$point" "$ASTRA_BIN" --yes install jq > "$TEST_DIR/$point.out" 2>&1
    status=$?
    set -e
    test "$status" -eq 77
    "$ASTRA_BIN" history > "$TEST_DIR/$point.history"
    actual=$(readlink "$XDG_STATE_HOME/astra/profiles/default/profile")
    recorded=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT store_path FROM generations WHERE active=1 AND profile_id=(SELECT id FROM profiles WHERE name='default');")
    test "$actual" = "$recorded"
    if [ "$point" = after_activation ] || [ "$point" = after_verification ] || [ "$point" = before_commit ] || [ "$point" = after_commit ]; then
        "$ASTRA_BIN" list | grep -F jq >/dev/null
        test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" -eq 2
    else
        if "$ASTRA_BIN" list | grep -F jq >/dev/null; then exit 1; fi
        test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" -eq 1
    fi
    "$ASTRA_BIN" --dry-run clean > "$TEST_DIR/$point.clean-preview"
    grep -F 'Retention policy: keep every recorded generation and GC root' "$TEST_DIR/$point.clean-preview" >/dev/null
    eligible=2
    case "$point" in
        after_build) eligible=1 ;;
        after_root|before_prepared_db) eligible=3 ;;
    esac
    grep -F "Eligible abandoned ASTRA links: $eligible" "$TEST_DIR/$point.clean-preview" >/dev/null
    retained=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT root_path FROM store_references LIMIT 1;")
    test -L "$retained"
    if [ "$eligible" -gt 0 ]; then
        "$ASTRA_BIN" clean > "$TEST_DIR/$point.unconfirmed" 2>&1 && exit 1
        grep -F 'cleanup confirmation required' "$TEST_DIR/$point.unconfirmed" >/dev/null
    fi
    "$ASTRA_BIN" --yes clean > "$TEST_DIR/$point.clean"
    test -L "$retained"
    test "$(find "$XDG_STATE_HOME/astra/profiles/default/staged" -type l | wc -l)" -eq 0
    "$ASTRA_BIN" --dry-run clean | grep -F 'Eligible abandoned ASTRA links: 0' >/dev/null
    "$ASTRA_BIN" history >/dev/null
done
for failure in BUILD STAGE ROOT ACTIVATION; do
    export XDG_STATE_HOME="$TEST_DIR/fail_$failure"
    "$ASTRA_BIN" repo update >/dev/null
    "$ASTRA_BIN" --yes install hello >/dev/null
    if [ "$failure" = BUILD ]; then
        env "ASTRA_MOCK_FAIL_$failure=1" "$ASTRA_BIN" --allow-build --yes install jq > "$TEST_DIR/fail_$failure.out" 2>&1 && exit 1
    else
        env "ASTRA_MOCK_FAIL_$failure=1" "$ASTRA_BIN" --yes install jq > "$TEST_DIR/fail_$failure.out" 2>&1 && exit 1
    fi
    "$ASTRA_BIN" history >/dev/null
    test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" -eq 1
done
