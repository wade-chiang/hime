#!/usr/bin/env bash
#
# Run by text-input-test (@exec) in a session: switch the input style as
# hime-setup does, saving it and telling the daemon to reload.
#
# Usage: set-style.sh 1|2   (OverSpot, Root)

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
printf '%s' "$1" >"$HOME/.config/hime/config/hime-input-style"
"$here/hime-client-test" -m "change font size"
