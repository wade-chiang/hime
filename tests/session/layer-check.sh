#!/usr/bin/env bash
#
# Inside a headless sway session (run-session.sh): type KEYS so that the
# input window is shown, screenshot it while the client stays connected, and
# report which of the points given in HIME_CHECK_POINTS show hime's window
# (anything but the color at HIME_CHECK_BACKGROUND, a point far from it).
#
# Usage: layer-check.sh KEY...

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

"$here/hime-client-test" "$@" @sleep 2000 &
client=$!
sleep 1
grim -t ppm "$HIME_SESSION_TMP/shot.ppm"
wait "$client"

# shellcheck disable=SC2086
python3 "$here/pixels.py" "$HIME_SESSION_TMP/shot.ppm" "$HIME_CHECK_BACKGROUND" $HIME_CHECK_POINTS
