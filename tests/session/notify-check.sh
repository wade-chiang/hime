#!/usr/bin/env bash
#
# Inside a session (run-session.sh): run PROGRAM with KEYS, then while it
# waits (@wait), make the daemon commit text and change the preedit the
# way mouse actions do, through its test hooks: HIME_NOTIFY_HOOKS, by
# default "commit 測試;preedit" (a symbol table click, a candidate click).
# PROGRAM prints what reaches it.
#
# Usage: notify-check.sh PROGRAM KEY...

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
program="$1"
shift

# PROGRAM creates HIME_TEST_READY when it starts waiting (after its keys,
# and focused) and stops waiting when HIME_TEST_DONE exists
export HIME_TEST_READY="$HIME_SESSION_TMP/notify-ready"
export HIME_TEST_DONE="$HIME_SESSION_TMP/notify-done"
rm -f "$HIME_TEST_READY" "$HIME_TEST_DONE"

"$here/$program" "$@" @wait 10000 &
client=$!

for _ in $(seq 100); do
    [[ -e "$HIME_TEST_READY" ]] && break
    sleep 0.1
done

# the actions, separated by ";" (see hime_test_hook in src/eve.c)
IFS=';' read -ra hooks <<<"${HIME_NOTIFY_HOOKS:-commit 測試;preedit}"
for hook in "${hooks[@]}"; do
    env -u HIME_TEST_READY "$here/hime-client-test" -m "#hime_test $hook"
    sleep 0.2
done
sleep 0.3
touch "$HIME_TEST_DONE"

wait "$client"
