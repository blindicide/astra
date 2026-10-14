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
export ASTRA_MOCK_CONFIG_FILE=$(realpath tests/fixtures/nix-config.json)
"$ASTRA_BIN" repo update >/dev/null
ASTRA_MOCK_MISSING_STORE=1 "$ASTRA_BIN" --yes install hello > "$TEST_DIR/missing.out" 2>&1 && exit 1
grep -F 'use --allow-build' "$TEST_DIR/missing.out"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions;")" = 0
ASTRA_MOCK_MISSING_STORE=1 ASTRA_MOCK_FAIL_BUILD=1 "$ASTRA_BIN" --allow-build --yes install hello > "$TEST_DIR/failed-build.out" 2>&1 && exit 1
grep -F 'Nix build or substitute' "$TEST_DIR/failed-build.out"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions;")" = 0
ASTRA_MOCK_REQUIRE_OFFLINE=1 "$ASTRA_BIN" --offline --yes install hello jq > "$TEST_DIR/install.out"
grep -F 'committed' "$TEST_DIR/install.out"
"$ASTRA_BIN" list > "$TEST_DIR/list.out"
grep -F 'hello' "$TEST_DIR/list.out"
grep -F 'jq' "$TEST_DIR/list.out"
"$ASTRA_BIN" why hello > "$TEST_DIR/why-direct.out"
grep -F 'direct ASTRA selection' "$TEST_DIR/why-direct.out"
"$ASTRA_BIN" --offline why sharedlib > "$TEST_DIR/why-closure.out"
test "$(grep -c 'runtime closure of' "$TEST_DIR/why-closure.out")" = 2
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
"$ASTRA_BIN" history show "$first_id" > "$TEST_DIR/show.out"
grep -F "Full hash: $first_hash" "$TEST_DIR/show.out"
grep -F 'prepared' "$TEST_DIR/show.out"
"$ASTRA_BIN" --json history show "$first_id" > "$TEST_DIR/show.json"
python3 -m json.tool "$TEST_DIR/show.json" > /dev/null
grep -F '"command":"history show"' "$TEST_DIR/show.json"
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
"$ASTRA_BIN" --json verify > "$TEST_DIR/verify.json"
grep -F '"profiles_verified":2' "$TEST_DIR/verify.json"
root_to_repair=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT root_path FROM store_references ORDER BY rowid LIMIT 1;")
test -L "$root_to_repair"
unlink "$root_to_repair"
"$ASTRA_BIN" --dry-run repair > "$TEST_DIR/repair-preview.out"
grep -F '1 missing committed GC root(s)' "$TEST_DIR/repair-preview.out"
"$ASTRA_BIN" repair > "$TEST_DIR/repair-unconfirmed.out" 2>&1 && exit 1
grep -F 'repair confirmation required' "$TEST_DIR/repair-unconfirmed.out"
"$ASTRA_BIN" --yes repair > "$TEST_DIR/repair.out"
test -L "$root_to_repair"
grep -F 'Profile reconciled and audited' "$TEST_DIR/repair.out"
"$ASTRA_BIN" history > /dev/null
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transaction_events WHERE event='root_restored';")" -eq 1
journal_to_test="$XDG_STATE_HOME/astra/profiles/default/journals/$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT hash FROM transactions ORDER BY sequence LIMIT 1;").json"
original_journal=$(cat "$journal_to_test")
mv "$journal_to_test" "$TEST_DIR/safe-journal"
ln -s "$TEST_DIR/safe-journal" "$journal_to_test"
"$ASTRA_BIN" history > "$TEST_DIR/symlink-journal.out" 2>&1 && exit 1
grep -F 'transaction journal is missing or unsafe' "$TEST_DIR/symlink-journal.out"
unlink "$journal_to_test"
mv "$TEST_DIR/safe-journal" "$journal_to_test"
unlink "$root_to_repair"
printf '%s' '{"tampered":true}' > "$journal_to_test"
"$ASTRA_BIN" --yes repair > "$TEST_DIR/repair-tampered.out" 2>&1 && exit 1
grep -F 'inconsistent root, intent, or journal' "$TEST_DIR/repair-tampered.out"
test ! -L "$root_to_repair"
printf '%s' "$original_journal" > "$journal_to_test"
"$ASTRA_BIN" --yes repair > /dev/null
test -L "$root_to_repair"
test -f "$XDG_STATE_HOME/astra/state.db.backup-v1"
mkdir -p "$TEST_DIR/next-fixture"
sed 's/0123456789abcdef0123456789abcdef01234567/1111111111111111111111111111111111111111/g' tests/fixtures/nix-metadata.json > "$TEST_DIR/next-fixture/nix-metadata.json"
sed 's/2.12.1/2.13.0/g; s/1.7.1/1.8.0/g' tests/fixtures/nix-search.json > "$TEST_DIR/next-fixture/nix-search.json"
export ASTRA_FIXTURE_DIR="$TEST_DIR/next-fixture"
profile_before_update=$(readlink "$XDG_STATE_HOME/astra/profiles/default/profile")
committed_before_update=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")
"$ASTRA_BIN" --json update > "$TEST_DIR/update-next.json"
grep -F '"command":"update"' "$TEST_DIR/update-next.json"
grep -F '"profiles_changed":false' "$TEST_DIR/update-next.json"
test "$(readlink "$XDG_STATE_HOME/astra/profiles/default/profile")" = "$profile_before_update"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" = "$committed_before_update"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM package_selections s JOIN generations g ON g.id=s.generation_id WHERE g.active=1 AND s.package_id IN (SELECT p.id FROM packages p JOIN repository_revisions r ON r.id=p.repository_revision_id WHERE r.revision='1111111111111111111111111111111111111111');")" = 0
"$ASTRA_BIN" --dry-run upgrade > "$TEST_DIR/upgrade.plan"
grep -F '1111111111111111111111111111111111111111' "$TEST_DIR/upgrade.plan"
"$ASTRA_BIN" --yes upgrade > "$TEST_DIR/upgrade.out"
grep -F committed "$TEST_DIR/upgrade.out"
"$ASTRA_BIN" list > "$TEST_DIR/upgraded.list"
grep -F dddddddddddddddddddddddddddddddd "$TEST_DIR/upgraded.list"
grep -F eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee "$TEST_DIR/upgraded.list"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed' AND intent LIKE '%\"operation\":\"upgrade\"%';")" = 1
"$ASTRA_BIN" --yes upgrade > "$TEST_DIR/no-upgrade.out" 2>&1 && exit 1
grep -F 'already use the current repository revision' "$TEST_DIR/no-upgrade.out"
last_generation=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT target_generation FROM transactions ORDER BY sequence DESC LIMIT 1;')
last_selection=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT output_path FROM package_selections WHERE generation_id=$last_generation LIMIT 1;")
sqlite3 "$XDG_STATE_HOME/astra/state.db" "UPDATE package_selections SET output_path='/nix/store/ffffffffffffffffffffffffffffffff-tampered' WHERE generation_id=$last_generation AND output_path='$last_selection';"
"$ASTRA_BIN" history > "$TEST_DIR/tamper.out" 2>&1 && exit 1
grep -F 'transaction selections differ from generation' "$TEST_DIR/tamper.out"
sqlite3 "$XDG_STATE_HOME/astra/state.db" "UPDATE package_selections SET output_path='$last_selection' WHERE generation_id=$last_generation AND output_path='/nix/store/ffffffffffffffffffffffffffffffff-tampered';"
last_sequence=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT max(sequence) FROM transactions;')
original_previous=$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT previous_generation FROM transactions WHERE sequence=$last_sequence;")
sqlite3 "$XDG_STATE_HOME/astra/state.db" "UPDATE transactions SET previous_generation=$last_generation WHERE sequence=$last_sequence;"
"$ASTRA_BIN" history > "$TEST_DIR/tamper-chain.out" 2>&1 && exit 1
grep -F 'transaction intent does not match its generation chain' "$TEST_DIR/tamper-chain.out"
sqlite3 "$XDG_STATE_HOME/astra/state.db" "UPDATE transactions SET previous_generation=$original_previous WHERE sequence=$last_sequence;"
sqlite3 "$XDG_STATE_HOME/astra/state.db" "UPDATE transactions SET target_generation=NULL WHERE sequence=$last_sequence;"
"$ASTRA_BIN" history > "$TEST_DIR/tamper-link.out" 2>&1 && exit 1
grep -F 'transaction generation link is missing' "$TEST_DIR/tamper-link.out"
sqlite3 "$XDG_STATE_HOME/astra/state.db" "UPDATE transactions SET target_generation=$last_generation WHERE sequence=$last_sequence;"
"$ASTRA_BIN" history > "$TEST_DIR/clean-history.out"
sqlite3 "$XDG_STATE_HOME/astra/state.db" "DELETE FROM transaction_events WHERE transaction_sequence=$last_sequence AND event='recovered_committed';"
"$ASTRA_BIN" history > "$TEST_DIR/tamper-events.out" 2>&1 && exit 1
grep -F 'transaction outcome events are incomplete' "$TEST_DIR/tamper-events.out"
"$ASTRA_BIN" --allow-build --yes --profile buildopt install hello > "$TEST_DIR/buildopt.out"
grep -F committed "$TEST_DIR/buildopt.out"
"$ASTRA_BIN" --profile buildopt list | grep -F hello
export XDG_STATE_HOME="$TEST_DIR/cache-state"
mkdir -p "$TEST_DIR/copied"
export ASTRA_MOCK_COPIED_DIR="$TEST_DIR/copied"
ASTRA_FIXTURE_DIR=$(realpath tests/fixtures) "$ASTRA_BIN" repo update > /dev/null
ASTRA_MOCK_MISSING_STORE=1 ASTRA_MOCK_CACHE_AVAILABLE=1 ASTRA_MOCK_FAIL_CACHE_TOUCH=1 \
    "$ASTRA_BIN" --offline --yes install hello > "$TEST_DIR/offline-cache.out" 2>&1 && exit 1
grep -F 'use --allow-build' "$TEST_DIR/offline-cache.out"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT count(*) FROM transactions;')" = 0
ASTRA_MOCK_MISSING_STORE=1 ASTRA_MOCK_CACHE_AVAILABLE=1 ASTRA_MOCK_BAD_SIGNATURE=1 ASTRA_MOCK_FAIL_BUILD=1 \
    "$ASTRA_BIN" --allow-build --yes install hello > "$TEST_DIR/bad-signature.out" 2>&1 && exit 1
grep -F 'signature rejected' "$TEST_DIR/bad-signature.out"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" 'SELECT count(*) FROM transactions;')" = 0
ASTRA_MOCK_MISSING_STORE=1 ASTRA_MOCK_CACHE_AVAILABLE=1 ASTRA_MOCK_FAIL_BUILD=1 \
    "$ASTRA_BIN" --yes install hello > "$TEST_DIR/cache-only.out"
grep -F committed "$TEST_DIR/cache-only.out"
test -f "$TEST_DIR/copied/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-hello"
test "$(sqlite3 "$XDG_STATE_HOME/astra/state.db" "SELECT count(*) FROM transactions WHERE status='committed';")" = 1
export XDG_STATE_HOME="$TEST_DIR/build-state"
ASTRA_FIXTURE_DIR=$(realpath tests/fixtures) "$ASTRA_BIN" repo update > /dev/null
ASTRA_MOCK_MISSING_STORE=1 ASTRA_MOCK_CACHE_AVAILABLE=0 "$ASTRA_BIN" --allow-build --yes install hello > "$TEST_DIR/consented-build.out"
grep -F committed "$TEST_DIR/consented-build.out"
