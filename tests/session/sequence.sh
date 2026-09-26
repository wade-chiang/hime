#!/usr/bin/env bash
#
# Inside a session (run-session.sh): run programs of tests/session one
# after the other, in the same session, e.g. two applications.
#
# Usage: sequence.sh PROGRAM ARG... [-- PROGRAM ARG...]...

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

command=()
for arg in "$@" --; do
    if [[ "$arg" == -- ]]; then
        [[ ${#command[@]} -gt 0 ]] && "$here/${command[0]}" "${command[@]:1}"
        command=()
    else
        command+=("$arg")
    fi
done
