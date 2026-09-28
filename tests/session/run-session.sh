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
# With HIME_SESSION_XWAYLAND=1, sway and KWin run an Xwayland, on which the
# daemon (still on Wayland) serves XIM.
#
# With HIME_SESSION_COMPOSITOR=kwin it is a headless KWin (virtual
# backend, no Xwayland), which starts the daemon itself as its input method
# (--inputmethod), as Plasma does; key injection and screenshots are
# allowed (KWIN_*_NO_PERMISSION_CHECKS).  Screenshots need its OpenGL
# compositing (a render node or llvmpipe), not QPainter.
#
# With HIME_SESSION_COMPOSITOR=gnome it is a headless GNOME Shell (unsafe
# mode, for its Eval and Screenshot D-Bus methods), whose IBus uses HIME's
# engine from the build tree (src/ibus) as the only input source; as in a
# GNOME session, the daemon runs on its Xwayland (no layer-shell).
#
# GTK and Qt applications pick up the HIME IM modules from the build tree.
# With HIME_SESSION_X11=1, COMMAND runs as an X11 client on the
# compositor's Xwayland instead.

set -euo pipefail
[[ -n "${HIME_SESSION_TRACE:-}" ]] && set -x

here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"

if [[ "${HIME_SESSION_INNER:-}" != 1 ]]; then
    compositor=mutter
    [[ "${HIME_SESSION_COMPOSITOR:-}" == sway ]] && compositor=sway
    [[ "${HIME_SESSION_COMPOSITOR:-}" == kwin ]] && compositor=kwin_wayland
    [[ "${HIME_SESSION_COMPOSITOR:-}" == gnome ]] && compositor=gnome-shell
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
    # not a remote session (gnome-shell hangs starting Xwayland when run
    # over SSH)
    unset SSH_CONNECTION SSH_CLIENT SSH_TTY
    # the private HOME's, not the user's (gsettings would write there)
    unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME XDG_STATE_HOME
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
    # KWin starts the daemon (in the foreground) on a connection of its own;
    # with an Xwayland, it tells the daemon the display
    inputmethod="env ${daemon_env[*]} $top/src/hime"
    xwayland=""
    if [[ "${HIME_SESSION_XWAYLAND:-}" == 1 ]]; then
        xwayland=--xwayland
        inputmethod="sh -c 'echo \"\$DISPLAY\" >$tmp/kwin-display; exec $inputmethod'"
    fi
    KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
        KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 \
        KWIN_XKB_DEFAULT_KEYMAP=true XKB_DEFAULT_LAYOUT=us \
        kwin_wayland --virtual --width 1280 --height 800 --socket wl-hime-test \
        --no-lockscreen --no-global-shortcuts --no-kactivities $xwayland \
        --inputmethod "$inputmethod" >"$log" 2>&1 &
elif [[ "${HIME_SESSION_COMPOSITOR:-}" == gnome ]]; then
    if [[ ! -x "$top/src/ibus/hime-ibus" ]]; then
        echo "run-session.sh: src/ibus/hime-ibus not built" >&2
        exit 77
    fi
    # not the logind session of whoever runs the tests
    export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_ID=hime-test-none
    # HIME as the only input source; nothing else steals the keys
    gsettings set org.gnome.desktop.input-sources sources "[('ibus', 'hime')]"
    gsettings set org.gnome.desktop.search-providers disable-external true
    gsettings set org.gnome.desktop.screensaver lock-enabled false
    gsettings set org.gnome.desktop.session idle-delay 0
    # rd-type.py clicks from the top left corner
    gsettings set org.gnome.desktop.interface enable-hot-corners false
    # HIME's engine from the build tree, next to IBus's own components
    mkdir -p "$tmp/ibus"
    sed "s|<exec>.*</exec>|<exec>/usr/bin/env LD_LIBRARY_PATH=$top/src/im-client $top/src/ibus/hime-ibus --ibus</exec>|" \
        "$top/src/ibus/hime.xml" >"$tmp/ibus/hime.xml"
    export IBUS_COMPONENT_PATH="$tmp/ibus:/usr/share/ibus/component"
    # GTK applications would start the document portal, a FUSE mount
    export GDK_DEBUG=no-portals
    # hime-ibus, started by IBus with the input source, must not start the
    # installed daemon before ours is up (it connects to ours later)
    export HIME_IM_CLIENT_NO_AUTO_EXEC=1
    gnome-shell --headless --wayland --unsafe-mode --wayland-display=wl-hime-test \
        --virtual-monitor 1280x800 >"$log" 2>&1 &
elif [[ "${HIME_SESSION_COMPOSITOR:-}" == sway ]]; then
    xwayland="xwayland disable"
    if [[ "${HIME_SESSION_XWAYLAND:-}" == 1 ]]; then
        # sway tells its clients the display (Xwayland's socket is in the
        # global /tmp/.X11-unix)
        xwayland="xwayland force
exec echo \"\$DISPLAY\" >$tmp/sway-display"
    fi
    printf '%s\n' 'output HEADLESS-1 resolution 1280x800 position 0 0' \
        'output HEADLESS-2 resolution 1280x800 position 1280 0' \
        "$xwayland" >"$tmp/sway.config"
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
keyboard_pid=""
portal_pid=""
cleanup() {
    [[ -n "$keyboard_pid" ]] && { kill "$keyboard_pid" 2>/dev/null || true; }
    [[ -n "$portal_pid" ]] && { kill "$portal_pid" 2>/dev/null || true; }
    kill $(hime_pids) 2>/dev/null || true
    kill "$mutter_pid" 2>/dev/null || true
    wait 2>/dev/null || true
    fusermount3 -u "$XDG_RUNTIME_DIR/doc" 2>/dev/null || true
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
elif [[ "${HIME_SESSION_COMPOSITOR:-}" == gnome ]]; then
    started='grep -q "GNOME Shell started" "$log"'
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
if [[ "${HIME_SESSION_XWAYLAND:-}" == 1 ]]; then
    display_file="$tmp/sway-display"
    [[ "${HIME_SESSION_COMPOSITOR:-}" == kwin ]] && display_file="$tmp/kwin-display"
    if ! wait_for 'grep -q : "$display_file" 2>/dev/null'; then
        echo "run-session.sh: the compositor started no Xwayland" >&2
        exit 77
    fi
    x_display="$(cat "$display_file")"
fi
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

if [[ "${HIME_SESSION_COMPOSITOR:-}" == gnome ]]; then
    shell_eval() {
        gdbus call --session -d org.gnome.Shell -o /org/gnome/Shell -m org.gnome.Shell.Eval "$1" 2>/dev/null
    }
    # Our daemon started Xwayland, and GNOME Shell restarts IBus with XIM
    # then, and with it HIME's engine: wait for that
    # (this session's: the user's may run with --xim too)
    ibus_xim() {
        local pid
        for pid in $(pgrep -f -- "ibus-daemon .*--xim"); do
            tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null | grep -qx "HIME_SESSION_TMP=$tmp" && return 0
        done
        return 1
    }
    wait_for ibus_xim || true
    # HIME's input source, through IBus
    if ! wait_for 'shell_eval "Main.inputMethod._currentSource?.id" | grep -q "hime" &&
            [[ "$(WAYLAND_DISPLAY=$wl_display ibus engine 2>/dev/null)" == hime ]]'; then
        echo "run-session.sh: the HIME input source did not become active" >&2
        cat "$log" >&2
        exit 1
    fi
    # a keyboard, which a headless GNOME has not: else no window gets the
    # keyboard focus
    "$here/rd-type.py" --keep &
    keyboard_pid=$!
    wait_for '[[ -p "$XDG_RUNTIME_DIR/rd-type.fifo" ]]' 
    # GNOME's portal backend, which asks GNOME Shell for the focused
    # application each time it changes (hime-ibus reads the replies)
    if [[ -x /usr/lib/xdg-desktop-portal-gnome ]]; then
        WAYLAND_DISPLAY="$wl_display" /usr/lib/xdg-desktop-portal-gnome >"$tmp/portal.log" 2>&1 &
        portal_pid=$!
        wait_for 'gdbus call --session -d org.freedesktop.DBus -o /org/freedesktop/DBus \
            -m org.freedesktop.DBus.NameHasOwner org.freedesktop.impl.portal.desktop.gnome |
            grep -q true' || true
    fi
    # the shell starts in the overview, where new windows get no focus
    shell_eval 'Main.overview.hide()' >/dev/null
    wait_for 'shell_eval "Main.overview.visible || Main.overview.animationInProgress" | grep -q "false"'
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
