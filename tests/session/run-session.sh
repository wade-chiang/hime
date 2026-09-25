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
# Xwayland, daemonized as when a client starts it.  Like a GNOME session,
# XMODIFIERS names IBus.  Requires mutter and dbus-run-session.
#
# The daemon's config can be adjusted with HIME_CONF="name=value ..."
#
# GTK and Qt applications pick up the HIME IM modules from the build tree.
# With HIME_SESSION_X11=1, COMMAND runs as an X11 client on mutter's
# Xwayland instead.

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
    export XMODIFIERS=@im=ibus
    exec dbus-run-session -- "$0" "$@"
fi

# ---- inside the private dbus session ----------------------------------------

tmp="$HIME_SESSION_TMP"
log="$tmp/mutter.log"

mutter --headless --wayland --wayland-display=wl-hime-test \
    --virtual-monitor 1280x800 >"$log" 2>&1 &
mutter_pid=$!

# the daemonized hime of this session (it is not our child)
hime_pids() {
    local pid
    for pid in $(pgrep -f "^$top/src/hime\$"); do
        tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null |
            grep -qx "HIME_SESSION_TMP=$tmp" && echo "$pid"
    done
}

# must not fail: under set -e that would replace the exit status
cleanup() {
    kill $(hime_pids) 2>/dev/null || true
    kill "$mutter_pid" 2>/dev/null || true
    wait 2>/dev/null || true
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
# HIME_DAEMON makes it daemonize, as when a client starts it.
DISPLAY="$x_display" XAUTHORITY="$x_auth" HIME_DAEMON=1 \
    HIME_TABLE_DIR="$top/data" \
    "$top/src/hime" >"$tmp/hime.log" 2>&1 </dev/null

if ! wait_for '[[ -S "$XDG_RUNTIME_DIR/hime/hime.socket" ]]'; then
    echo "run-session.sh: hime did not open its socket" >&2
    cat "$tmp/hime.log" >&2
    exit 1
fi

export WAYLAND_DISPLAY=wl-hime-test
export LD_LIBRARY_PATH="$top/src/im-client${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export HIME_IM_CLIENT_NO_AUTO_EXEC=1

export GTK_IM_MODULE=hime
if [[ -f "$top/src/gtk3-im/im-hime.so" ]]; then
    gtk-query-immodules-3.0 "$top/src/gtk3-im/im-hime.so" >"$tmp/immodules.cache"
    export GTK_IM_MODULE_FILE="$tmp/immodules.cache"
fi

# GTK 4 scans $GTK_PATH/4.0.0/immodules for GIO modules
if [[ -f "$top/src/gtk4-im/libim-hime.so" ]]; then
    mkdir -p "$tmp/gtk-path/4.0.0/immodules"
    ln -sf "$top/src/gtk4-im/libim-hime.so" "$tmp/gtk-path/4.0.0/immodules/"
    export GTK_PATH="$tmp/gtk-path"
fi

# Qt finds platform input contexts in $QT_PLUGIN_PATH/platforminputcontexts;
# Qt 5 and Qt 6 each skip the other's plugin.
export QT_IM_MODULE=hime QT_QPA_PLATFORM=wayland
qt_plugin_path=""
for qt in qt5 qt6; do
    if [[ -f "$top/src/$qt-im/im-hime.so" ]]; then
        mkdir -p "$tmp/$qt-plugins/platforminputcontexts"
        ln -sf "$top/src/$qt-im/im-hime.so" "$tmp/$qt-plugins/platforminputcontexts/"
        qt_plugin_path="$qt_plugin_path${qt_plugin_path:+:}$tmp/$qt-plugins"
    fi
done
export QT_PLUGIN_PATH="$qt_plugin_path"

if [[ "${HIME_SESSION_X11:-}" == 1 ]]; then
    export DISPLAY="$x_display" XAUTHORITY="$x_auth" GDK_BACKEND=x11 QT_QPA_PLATFORM=xcb
fi

"$@"
