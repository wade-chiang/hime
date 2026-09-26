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
# The daemon's config can be adjusted with HIME_CONF="name=value ...", and
# HIME_SESSION_METHOD=intcode, chewing or anthy makes that module (from the
# build tree) the default input method, and pho or tsin the Zhuyin or
# phrase (詞音) method.
#
# With HIME_SESSION_DAEMON_BACKEND=wayland the daemon is forced onto GDK's Wayland
# backend with no X display at all.
#
# With HIME_SESSION_COMPOSITOR=sway the session is a headless sway instead:
# it supports layer-shell, has no Xwayland, and the daemon runs on the
# Wayland backend.  Screenshots can be taken with grim.  With
# HIME_SESSION_OUTPUTS=2 it has two 1280x800 outputs side by side.
#
# With HIME_SESSION_COMPOSITOR=kwin it is a headless KWin (virtual
# backend, QPainter, no Xwayland), which starts the daemon itself as its
# input method (--inputmethod), as Plasma does; key injection and
# screenshots are allowed (KWIN_*_NO_PERMISSION_CHECKS).
#
# GTK and Qt applications pick up the HIME IM modules from the build tree.
# With HIME_SESSION_X11=1, COMMAND runs as an X11 client on mutter's
# Xwayland instead.

set -euo pipefail
[[ -n "${HIME_SESSION_TRACE:-}" ]] && set -x

here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"

if [[ "${HIME_SESSION_INNER:-}" != 1 ]]; then
    compositor=mutter
    [[ "${HIME_SESSION_COMPOSITOR:-}" == sway ]] && compositor=sway
    [[ "${HIME_SESSION_COMPOSITOR:-}" == kwin ]] && compositor=kwin_wayland
    for bin in "$compositor" dbus-run-session; do
        if ! command -v "$bin" >/dev/null; then
            echo "run-session.sh: $bin not found" >&2
            exit 77
        fi
    done

    # The inner cleanup removes it: the exec below drops this trap, which
    # only covers failures before that.
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    mkdir -m 700 "$tmp/runtime"
    mkdir -p "$tmp/home/.config/hime/config"

    conf="$tmp/home/.config/hime"
    cp "$top/tests/gtab/tables/test-liu.cin" "$conf/"
    (cd "$conf" && "$top/src/hime-cin2gtab" test-liu.cin >/dev/null)
    printf 'test 1 test-liu.gtab -\n' >"$conf/gtab.list"
    printf '1 test-liu.gtab' >"$conf/config/default-input-method"
    case "${HIME_SESSION_METHOD:-}" in
    intcode) module="0 intcode-module.so" ;;
    chewing) module="[ chewing-module.so" ;;
    anthy) module="= anthy-module.so" ;;
    pho) module="3 !PHO" ;;
    tsin) module="6 !TSIN" ;;
    *) module="" ;;
    esac
    if [[ -n "$module" && "$module" != *'!'* && ! -f "$top/src/modules/${module#* }" ]]; then
        echo "run-session.sh: ${module#* } not built" >&2
        exit 77
    fi
    if [[ -n "$module" ]]; then
        printf '%s %s -\n' "${HIME_SESSION_METHOD}" "$module" >>"$conf/gtab.list"
        printf '%s' "$module" >"$conf/config/default-input-method"
    fi
    printf '1' >"$conf/config/hime-init-im-enabled"
    for kv in ${HIME_CONF:-}; do
        printf '%s' "${kv#*=}" >"$conf/config/${kv%%=*}"
    done

    export HIME_SESSION_INNER=1 HIME_SESSION_TMP="$tmp"
    export XDG_RUNTIME_DIR="$tmp/runtime" HOME="$tmp/home"
    unset DISPLAY WAYLAND_DISPLAY XAUTHORITY
    export XMODIFIERS=@im=ibus
    # no gvfs daemons, which would leave a gvfs directory behind
    export GIO_USE_VFS=local
    exec dbus-run-session -- "$0" "$@"
fi

# ---- inside the private dbus session ----------------------------------------

tmp="$HIME_SESSION_TMP"
log="$tmp/compositor.log"

# The daemon: HIME_DAEMON makes it daemonize, as when a client starts it.
# It sees the session as a desktop would show it (Wayland, plus X on
# mutter) and picks its backend itself: X11 on mutter, Wayland on sway,
# which has layer-shell.  HIME_TEST_HOOKS: tests/session/notify-check.sh
# drives mouse actions.
daemon_env=(HIME_TABLE_DIR="$top/data" HIME_TEST_HOOKS=1 HIME_MODULE_DIR="$top/src/modules")

if [[ "${HIME_SESSION_COMPOSITOR:-}" == kwin ]]; then
    # KWin starts the daemon (in the foreground) on a connection of its own
    KWIN_COMPOSE=Q KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
        KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 \
        KWIN_XKB_DEFAULT_KEYMAP=true XKB_DEFAULT_LAYOUT=us \
        kwin_wayland --virtual --width 1280 --height 800 --socket wl-hime-test \
        --no-lockscreen --no-global-shortcuts --no-kactivities \
        --inputmethod "env ${daemon_env[*]} $top/src/hime" >"$log" 2>&1 &
elif [[ "${HIME_SESSION_COMPOSITOR:-}" == sway ]]; then
    printf '%s\n' 'output HEADLESS-1 resolution 1280x800 position 0 0' \
        'output HEADLESS-2 resolution 1280x800 position 1280 0' \
        'xwayland disable' >"$tmp/sway.config"
    # sway names its socket itself (wayland-N, the only one in our
    # private XDG_RUNTIME_DIR); there is no swaybg for the background
    WLR_HEADLESS_OUTPUTS="${HIME_SESSION_OUTPUTS:-1}" \
        WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 \
        WLR_RENDERER=pixman sway -c "$tmp/sway.config" >"$log" 2>&1 &
else
    mutter --headless --wayland --wayland-display=wl-hime-test \
        --virtual-monitor 1280x800 >"$log" 2>&1 &
fi
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
    rm -rf "$tmp" 2>/dev/null || true
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

if [[ "${HIME_SESSION_COMPOSITOR:-}" == sway ]]; then
    started='compgen -G "$XDG_RUNTIME_DIR/wayland-[0-9]" >/dev/null'
elif [[ "${HIME_SESSION_COMPOSITOR:-}" == kwin ]]; then
    started='[[ -S "$XDG_RUNTIME_DIR/wl-hime-test" ]]'
else
    started='[[ -S "$XDG_RUNTIME_DIR/wl-hime-test" ]] && grep -q "public X11 display" "$log"'
fi
if ! wait_for "$started"; then
    echo "run-session.sh: the compositor did not start" >&2
    cat "$log" >&2
    exit 1
fi

wl_display=wl-hime-test
if [[ "${HIME_SESSION_COMPOSITOR:-}" == sway ]]; then
    wl_display="$(basename "$(compgen -G "$XDG_RUNTIME_DIR/wayland-[0-9]" | head -1)")"
fi

x_display="$(sed -n 's/.*Using public X11 display \(:[0-9]*\).*/\1/p' "$log" | head -1)"
x_auth="$(ls "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -1 || true)"

daemon_env+=(WAYLAND_DISPLAY="$wl_display" HIME_DAEMON=1)
if [[ "${HIME_SESSION_DAEMON_BACKEND:-}" == wayland ]]; then
    daemon_env+=(HIME_BACKEND=wayland)
elif [[ -n "$x_display" ]]; then
    daemon_env+=(DISPLAY="$x_display" XAUTHORITY="$x_auth")
fi
if [[ "${HIME_SESSION_COMPOSITOR:-}" != kwin ]]; then
    env -u DISPLAY "${daemon_env[@]}" "$top/src/hime" >"$tmp/hime.log" 2>&1 </dev/null
fi

if ! wait_for '[[ -S "$XDG_RUNTIME_DIR/hime/hime.socket" ]]'; then
    echo "run-session.sh: hime did not open its socket" >&2
    cat "$tmp/hime.log" "$log" >&2 2>/dev/null || true
    exit 1
fi

export WAYLAND_DISPLAY="$wl_display"
export LD_LIBRARY_PATH="$top/src/im-client${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export HIME_IM_CLIENT_NO_AUTO_EXEC=1

# Resolve all symbols at load time, as binaries linked with -z now (the
# default of Arch makepkg) do: a module with a missing symbol must fail
# here, not only in the packaged build.
export LD_BIND_NOW=1

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
