#!/usr/bin/env bash
#
# Session tests: each cases/NAME.keys is typed by hime-client-test inside a
# headless mutter session (run-session.sh); the output must match
# cases/NAME.expected.  Lines starting with # are comments.
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
    read -ra args <<<"$(grep -v "^#" "$keys" | tr "\n" " ")"

    if ! "$here/run-session.sh" "$here/hime-client-test" "${args[@]}" \
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
