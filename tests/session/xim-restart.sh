#!/usr/bin/env bash
#
# Inside a session with an Xwayland (run-session.sh, @xwayland @x11): type
# KEYS through XIM (xim-test), make Xwayland go away (sway starts another
# one), and type them again: the daemon, on Wayland, serves XIM on the new
# X server.
#
# Usage: xim-restart.sh KEYS...

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

"$here/xim-test" "$@"

# this session's Xwayland, not another one on the machine
xwayland=""
for pid in $(pgrep -x Xwayland); do
    tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null | grep -qx "HIME_SESSION_TMP=$HIME_SESSION_TMP" &&
        xwayland="$pid"
done
if [[ -z "$xwayland" ]]; then
    echo "xim-restart.sh: no Xwayland in this session" >&2
    exit 1
fi
# wlroots starts another one only if this one ran for 5 s
while (($(ps -o etimes= -p "$xwayland") < 6)); do
    sleep 0.5
done
kill "$xwayland"

# the daemon tries again after 2 s, then 4, ...
for _ in $(seq 20); do
    sleep 1
    # (a try that fails prints what it typed too)
    if output="$("$here/xim-test" "$@" 2>/dev/null)"; then
        echo "$output"
        exit 0
    fi
done
echo "xim-restart.sh: no XIM on the new X server" >&2
exit 1
