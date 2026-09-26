#!/usr/bin/env bash
#
# Inside a session (run-session.sh): type KEYS into FeatherPad, a real Qt 6
# editor, through the HIME Qt module, and report whether it survived.
# Exits 77 without featherpad.
#
# Usage: featherpad-check.sh KEY...

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

if ! command -v featherpad >/dev/null; then
    echo "featherpad-check.sh: featherpad not found" >&2
    exit 77
fi

export QT_IM_MODULE=hime QT_IM_MODULES=hime
featherpad >"$HIME_SESSION_TMP/featherpad.log" 2>&1 &
editor=$!
# its window gets the focus (a headless sway has nothing else)
sleep 3
"$here/wl-type" "$@"
sleep 1
if kill -0 "$editor" 2>/dev/null; then
    echo "featherpad: running"
    kill "$editor"
else
    status=0
    wait "$editor" || status=$?
    echo "featherpad: exited with status $status"
fi
