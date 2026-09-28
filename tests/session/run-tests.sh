#!/usr/bin/env bash
#
# Session tests: each cases/NAME.keys is typed inside a headless mutter
# session (run-session.sh); the output must match cases/NAME.expected.
# Lines starting with # are comments.  Directives:
#
#   @program NAME   the client typing the keys (default hime-client-test,
#                   a client without an X display; gtk3-im-test,
#                   gtk4-im-test, qt5-im-test and qt6-im-test go through
#                   the IM modules; gtk3-text-input-test,
#                   gtk4-text-input-test and qt6-text-input-test use
#                   text-input, with keys typed on the seat by wl-type)
#   @x11            run the client on Xwayland instead of Wayland
#   @tool NAME      run src/NAME (a hime tool, as a Wayland client) first
#   @exit N         the client's expected exit status (default 0)
#   @env VAR=VALUE  set an environment variable for the client
#   @daemon-wayland run the daemon on GDK's Wayland backend, without X
#   @compositor sway run in a headless sway (layer-shell, no Xwayland)
#                   instead of mutter
#   @conf NAME=VALUE write a hime config value
#   @outputs N      number of outputs of the sway session
#   @xwayland       give the sway or KWin session an Xwayland (with @x11, the
#                   client runs there)
#   @method NAME    make a module (intcode, chewing, anthy), Zhuyin (pho)
#                   or tsin the default input method
#
# Usage: run-tests.sh [--update] [CASE.keys...]
# Exits 77 (skipped) when mutter is not available; a case whose compositor
# or tools are missing (exit status 77) is skipped.

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"

update=0
if [[ "${1:-}" == "--update" ]]; then
    update=1
    shift
fi

if [[ $# -gt 0 ]]; then
    cases=("$@")
else
    cases=("$here"/cases/*.keys)
fi

if ! command -v mutter >/dev/null; then
    echo "session tests: skipped (mutter not found)"
    exit 77
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0
skip=0
for keys in "${cases[@]}"; do
    name="$(basename "$keys" .keys)"
    expected="${keys%.keys}.expected"
    read -ra args <<<"$(grep -v -e "^#" -e "^@" "$keys" | tr "\n" " ")"

    program=hime-client-test
    x11=""
    tool=""
    exit_status=0
    envs=()
    daemon_backend=""
    compositor=""
    outputs=""
    xwayland=""
    method=""
    confs=""
    while read -r directive arg; do
        case "$directive" in
        @program) program="$arg" ;;
        @x11) x11=1 ;;
        @tool) tool="$arg" ;;
        @exit) exit_status="$arg" ;;
        @env) envs+=("$arg") ;;
        @daemon-wayland) daemon_backend=wayland ;;
        @compositor) compositor="$arg" ;;
        @outputs) outputs="$arg" ;;
        @xwayland) xwayland=1 ;;
        @method) method="$arg" ;;
        @conf) confs="$confs $arg" ;;
        esac
    done < <(grep '^@' "$keys")

    cmd=("$here/$program" "${args[@]}")
    if [[ -n "$tool" ]]; then
        cmd=(sh -c '"$0" && exec "$@"' "$top/src/$tool" "${cmd[@]}")
    fi
    if [[ ${#envs[@]} -gt 0 ]]; then
        cmd=(env "${envs[@]}" "${cmd[@]}")
    fi

    if [[ ! -x "$here/$program" ]]; then
        echo "skip $name ($program not built)"
        skip=$((skip + 1))
        continue
    fi

    status=0
    HIME_SESSION_X11="$x11" HIME_SESSION_DAEMON_BACKEND="$daemon_backend" \
        HIME_SESSION_COMPOSITOR="$compositor" HIME_SESSION_OUTPUTS="$outputs" \
        HIME_SESSION_XWAYLAND="$xwayland" \
        HIME_SESSION_METHOD="$method" \
        HIME_CONF="$confs" \
        "$here/run-session.sh" "${cmd[@]}" \
        >"$tmp/$name.actual" 2>"$tmp/$name.stderr" || status=$?
    # 77: a tool the case needs (sway, grim, ...) is missing
    if [[ $status -eq 77 && $exit_status -ne 77 ]]; then
        echo "skip $name ($(tail -1 "$tmp/$name.stderr"))"
        skip=$((skip + 1))
        continue
    fi
    if [[ $status -ne $exit_status ]]; then
        echo "FAIL $name (exit status $status, expected $exit_status)"
        grep -v -e dbus-daemon -e "connection to the bus" "$tmp/$name.stderr" || true
        fail=$((fail + 1))
        continue
    fi

    if [[ $update -eq 1 ]]; then
        cp "$tmp/$name.actual" "$expected"
        echo "updated $name"
    elif diff -u "$expected" "$tmp/$name.actual"; then
        pass=$((pass + 1))
    else
        echo "FAIL $name"
        fail=$((fail + 1))
    fi
done

[[ $update -eq 1 ]] || echo "session tests: $pass passed, $fail failed, $skip skipped"
[[ $fail -eq 0 ]]
