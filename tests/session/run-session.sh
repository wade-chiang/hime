#!/usr/bin/env bash
#
# Run a command inside a private headless GNOME/Mutter session with a HIME
# daemon from the build tree, as a native Wayland application would see it.
#
# Usage: run-session.sh COMMAND [ARG...]
#
# COMMAND runs with WAYLAND_DISPLAY set and DISPLAY unset, a private
# XDG_RUNTIME_DIR and HOME, and HIME typing the synthetic Boshiamy-shaped
# table from tests/gtab/tables.  The daemon itself runs on mutter's
# Xwayland.  Requires mutter and dbus-run-session.
#
# The daemon's config can be adjusted with HIME_CONF="name=value ..."

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"

if [[ "${HIME_SESSION_INNER:-}" != 1 ]]; then
    for bin in mutter dbus-run-session; do
        if ! command -v "$bin" >/dev/null; then
            echo "run-session.sh: $bin not found" >&2
            exit 77
        fi
    done

    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    mkdir -m 700 "$tmp/runtime"
    mkdir -p "$tmp/home/.config/hime/config"

    conf="$tmp/home/.config/hime"
    cp "$top/tests/gtab/tables/test-liu.cin" "$conf/"
    (cd "$conf" && "$top/src/hime-cin2gtab" test-liu.cin >/dev/null)
    printf 'test 1 test-liu.gtab -\n' >"$conf/gtab.list"
    printf '1 test-liu.gtab' >"$conf/config/default-input-method"
    printf '1' >"$conf/config/hime-init-im-enabled"
    for kv in ${HIME_CONF:-}; do
        printf '%s' "${kv#*=}" >"$conf/config/${kv%%=*}"
    done

    export HIME_SESSION_INNER=1 HIME_SESSION_TMP="$tmp"
    export XDG_RUNTIME_DIR="$tmp/runtime" HOME="$tmp/home"
    unset DISPLAY WAYLAND_DISPLAY XAUTHORITY
    exec dbus-run-session -- "$0" "$@"
fi

# ---- inside the private dbus session ----------------------------------------

tmp="$HIME_SESSION_TMP"
log="$tmp/mutter.log"

mutter --headless --wayland --wayland-display=wl-hime-test \
    --virtual-monitor 1280x800 >"$log" 2>&1 &
mutter_pid=$!

hime_pid=""
cleanup() {
    [[ -n "$hime_pid" ]] && kill "$hime_pid" 2>/dev/null
    kill "$mutter_pid" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

wait_for() {
    local i
    for i in $(seq 100); do
        eval "$1" && return 0
        sleep 0.1
    done
    return 1
}

if ! wait_for '[[ -S "$XDG_RUNTIME_DIR/wl-hime-test" ]] && grep -q "public X11 display" "$log"'; then
    echo "run-session.sh: mutter did not start" >&2
    cat "$log" >&2
    exit 1
fi

x_display="$(sed -n 's/.*Using public X11 display \(:[0-9]*\).*/\1/p' "$log" | head -1)"
x_auth="$(ls "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -1)"

# The daemon still needs X for its windows; it picks the X11 backend
# itself, which run-session.sh relies on by not setting GDK_BACKEND.
DISPLAY="$x_display" XAUTHORITY="$x_auth" \
    HIME_TABLE_DIR="$top/data" \
    "$top/src/hime" >"$tmp/hime.log" 2>&1 &
hime_pid=$!

if ! wait_for '[[ -S "$XDG_RUNTIME_DIR/hime/hime.socket" ]]'; then
    echo "run-session.sh: hime did not open its socket" >&2
    cat "$tmp/hime.log" >&2
    exit 1
fi

export WAYLAND_DISPLAY=wl-hime-test
export LD_LIBRARY_PATH="$top/src/im-client${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export HIME_IM_CLIENT_NO_AUTO_EXEC=1
"$@"
