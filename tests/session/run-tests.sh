#!/usr/bin/env bash
#
# Session tests: each cases/NAME.keys is typed inside a headless mutter
# session (run-session.sh); the output must match cases/NAME.expected.
# Lines starting with # are comments.  Directives:
#
#   @program NAME   the client typing the keys (default hime-client-test,
#                   a client without an X display; gtk3-im-test,
#                   gtk4-im-test, qt5-im-test and qt6-im-test go through
#                   the IM modules)
#   @x11            run the client on Xwayland instead of Wayland
#
# Usage: run-tests.sh [--update]
# Exits 77 (skipped) when mutter is not available.

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

update=0
[[ "${1:-}" == "--update" ]] && update=1

if ! command -v mutter >/dev/null; then
    echo "session tests: skipped (mutter not found)"
    exit 77
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0
for keys in "$here"/cases/*.keys; do
    name="$(basename "$keys" .keys)"
    expected="${keys%.keys}.expected"
    read -ra args <<<"$(grep -v -e "^#" -e "^@" "$keys" | tr "\n" " ")"

    program=hime-client-test
    x11=""
    while read -r directive arg; do
        case "$directive" in
        @program) program="$arg" ;;
        @x11) x11=1 ;;
        esac
    done < <(grep '^@' "$keys")

    if [[ ! -x "$here/$program" ]]; then
        echo "skip $name ($program not built)"
        continue
    fi

    if ! HIME_SESSION_X11="$x11" "$here/run-session.sh" "$here/$program" "${args[@]}" \
        >"$tmp/$name.actual" 2>"$tmp/$name.stderr"; then
        echo "FAIL $name"
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

[[ $update -eq 1 ]] || echo "session tests: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
