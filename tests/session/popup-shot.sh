#!/usr/bin/env bash
#
# Run by text-input-test (@exec) in a headless sway or KWin session: screenshot
# the screen and report which of the POINTS show hime's window (anything
# but the color at HIME_CHECK_BACKGROUND, a point of the test window far
# from it).  The first point must be one where the window is expected: the
# screenshot is retaken until it shows up, for at most 5 seconds.  Exits
# 77 without grim.
#
# Usage: popup-shot.sh X,Y...

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

# KWin has no screencopy protocol for grim: its D-Bus interface instead
screenshot() {
    if [[ "${HIME_SESSION_COMPOSITOR:-}" == kwin ]]; then
        "$here/kwin-shot.py" "$1"
    else
        grim -t ppm "$1"
    fi
}
if [[ "${HIME_SESSION_COMPOSITOR:-}" != kwin ]] && ! command -v grim >/dev/null; then
    echo "popup-shot.sh: grim not found" >&2
    exit 77
fi

shot="$HIME_SESSION_TMP/shot.ppm"
for _ in $(seq 25); do
    sleep 0.2
    screenshot "$shot"
    python3 "$here/pixels.py" "$shot" "$HIME_CHECK_BACKGROUND" "$1" | grep -q ' window$' && break
done
python3 "$here/pixels.py" "$shot" "$HIME_CHECK_BACKGROUND" "$@"
