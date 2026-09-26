#!/usr/bin/env bash
#
# Inside a headless sway session (run-session.sh): type KEYS so that the
# input window is shown, screenshot it while the client stays connected, and
# report which of the points given in HIME_CHECK_POINTS show hime's window
# (anything but the color at HIME_CHECK_BACKGROUND, a point far from it).
# The first point must be one where the window is expected; the screenshot
# is retaken until it shows up, for at most 5 seconds.  Exits 77 without
# grim or swaymsg.
#
# Usage: layer-check.sh KEY...

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

for tool in grim swaymsg python3; do
    if ! command -v "$tool" >/dev/null; then
        echo "layer-check.sh: $tool not found" >&2
        exit 77
    fi
done

"$here/hime-client-test" "$@" @sleep 10000 &
client=$!

# Screenshot until the first point shows the window, for at most 5 s.
shot="$HIME_SESSION_TMP/shot.ppm"
# shellcheck disable=SC2086
set -- $HIME_CHECK_POINTS
for _ in $(seq 25); do
    sleep 0.2
    grim -t ppm "$shot"
    python3 "$here/pixels.py" "$shot" "$HIME_CHECK_BACKGROUND" "$1" | grep -q ' window$' && break
done

# While the client is connected, hime's windows are shown.  They are layer
# surfaces: sway must not list any of them as a window.
SWAYSOCK="$(compgen -G "$XDG_RUNTIME_DIR/sway-ipc.*.sock" | head -1)"
export SWAYSOCK
windows="$(swaymsg -r -t get_tree | python3 -c '
import json, sys
def walk(node):
    yield node
    for child in node.get("nodes", []) + node.get("floating_nodes", []):
        yield from walk(child)
print(sum(1 for n in walk(json.load(sys.stdin)) if (n.get("app_id") or "").lower() == "hime"))')"

kill "$client"
wait "$client" 2>/dev/null || true

python3 "$here/pixels.py" "$shot" "$HIME_CHECK_BACKGROUND" "$@"
echo "hime windows: $windows"
