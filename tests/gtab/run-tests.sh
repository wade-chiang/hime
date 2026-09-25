#!/usr/bin/env bash
#
# Run the gtab characterization tests.
#
# Every cases/NAME.keys is a key script for harness.c; its transcript must
# match cases/NAME.expected exactly.  A case starts with directives:
#
#   @table FILE.gtab         table to type with (required)
#   @conf  NAME VALUE        write a hime config value before loading
#   @hime-conf FILE          use FILE (relative to the case) as hime.conf
#   @phrase-db               prebuild the table's phrase database, which
#                            hime would otherwise build with the installed
#                            tools under HIME_BIN_DIR
#
# FILE.gtab is looked up in tables/, then local/ (as FILE.cin, built with
# hime-cin2gtab, or as FILE.gtab), then the data/ directory of the source
# tree.  local/ is ignored by git: put tables that cannot be distributed,
# such as your own Boshiamy table, and the cases that use them there, and
# run them with: run-tests.sh local/*.keys
#
# Usage: run-tests.sh [--update] [CASE...]
#   --update   rewrite the .expected files from the current behavior
#
# HARNESS and CIN2GTAB may be set to override the default binary paths.

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
harness="${HARNESS:-$here/gtab-harness}"
cin2gtab="${CIN2GTAB:-$top/src/hime-cin2gtab}"
tsin2gtab_phrase="$top/src/hime-tsin2gtab-phrase"
tsa2d32="$top/src/hime-tsa2d32"

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

for bin in "$harness" "$cin2gtab" "$tsin2gtab_phrase" "$tsa2d32"; do
    if [[ ! -x "$bin" ]]; then
        echo "missing $bin; run 'make check' from the top directory first" >&2
        exit 2
    fi
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0

for keys in "${cases[@]}"; do
    name="$(basename "$keys" .keys)"
    expected="${keys%.keys}.expected"
    home="$tmp/$name"
    conf="$home/.config/hime"
    mkdir -p "$conf/config"

    table=""
    phrase_db=0
    while read -r directive arg1 arg2; do
        case "$directive" in
        @table) table="$arg1" ;;
        @conf) printf '%s' "$arg2" >"$conf/config/$arg1" ;;
        @hime-conf) cp "$(dirname "$keys")/$arg1" "$conf/hime.conf" ;;
        @phrase-db) phrase_db=1 ;;
        esac
    done < <(grep '^@' "$keys")

    if [[ -z "$table" ]]; then
        echo "$name: no @table directive" >&2
        exit 2
    fi

    for dir in "$here/tables" "$here/local"; do
        cin="$dir/${table%.gtab}.cin"
        if [[ -f "$cin" ]]; then
            cp "$cin" "$conf/"
            (cd "$conf" && "$cin2gtab" "$(basename "$cin")" >/dev/null)
            break
        elif [[ -f "$dir/$table" ]]; then
            cp "$dir/$table" "$conf/"
            break
        fi
    done

    if [[ $phrase_db -eq 1 ]]; then
        gtab="$conf/$table"
        [[ -f "$gtab" ]] || gtab="$top/data/$table"
        db="$conf/$table.tsin-db"
        # hime-tsin2gtab-phrase only reads tsin32 from the user directory
        cp "$top/data/tsin32" "$top/data/tsin32.idx" "$conf/"
        export HIME_TABLE_DIR="$top/data"
        HOME="$home" "$tsin2gtab_phrase" "$gtab" "$db.src" >/dev/null
        HOME="$home" LD_LIBRARY_PATH="$top/src/im-client" "$tsa2d32" "$db.src" "$db" >/dev/null 2>&1
    fi

    # A one-entry gtab.list makes the table under test the only method.
    printf 'test 1 %s -\n' "$table" >"$conf/gtab.list"

    actual="$tmp/$name.actual"
    grep -v '^@' "$keys" |
        HOME="$home" HIME_TABLE_DIR="$top/data" LC_ALL=C.UTF-8 \
            "$harness" "$table" >"$actual" 2>"$tmp/$name.stderr" || {
        echo "FAIL $name (harness exited $?)"
        cat "$tmp/$name.stderr"
        fail=$((fail + 1))
        continue
    }

    if [[ $update -eq 1 ]]; then
        cp "$actual" "$expected"
        echo "updated $name"
    elif diff -u "$expected" "$actual" >"$tmp/$name.diff" 2>&1; then
        pass=$((pass + 1))
    else
        echo "FAIL $name"
        cat "$tmp/$name.diff"
        fail=$((fail + 1))
    fi
done

if [[ $update -eq 0 ]]; then
    echo "gtab tests: $pass passed, $fail failed"
fi
[[ $fail -eq 0 ]]
