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

"$here/$program" "$@" @wait 3000 &
client=$!

# the actions, separated by ";" (see hime_test_hook in src/eve.c)
IFS=';' read -ra hooks <<<"${HIME_NOTIFY_HOOKS:-commit 測試;preedit}"
sleep 1
for hook in "${hooks[@]}"; do
    "$here/hime-client-test" -m "#hime_test $hook"
    sleep 0.3
done

wait "$client"
