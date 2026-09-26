#!/usr/bin/env bash
#
# Inside a session (run-session.sh): start another daemon the way KWin
# starts its input method, with a compositor connection of its own in
# WAYLAND_SOCKET, while the session's daemon runs.  It must replace that
# one; then run hime-client-test with KEYS, which connects again.
#
# Usage: compositor-launch.sh KEY...

set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
sock="$XDG_RUNTIME_DIR/hime/hime.socket"

# the daemon listening on the socket
listener() {
    python3 -c '
import socket, struct, sys
s = socket.socket(socket.AF_UNIX)
try:
    s.connect(sys.argv[1])
except OSError:
    print(0)
    sys.exit()
pid, uid, gid = struct.unpack("3i", s.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
print(pid)' "$sock"
}

old="$(listener)"
python3 -c '
import os, socket, subprocess, sys
s = socket.socket(socket.AF_UNIX)
s.connect(os.path.join(os.environ["XDG_RUNTIME_DIR"], os.environ["WAYLAND_DISPLAY"]))
env = dict(os.environ, WAYLAND_SOCKET=str(s.fileno()),
           HIME_TABLE_DIR=sys.argv[2] + "/data", HIME_TEST_HOOKS="1",
           HIME_MODULE_DIR=sys.argv[2] + "/src/modules")
subprocess.Popen([sys.argv[1]], env=env, pass_fds=[s.fileno()],
                 stdout=subprocess.DEVNULL, stderr=open(sys.argv[3], "w"))
' "$top/src/hime" "$top" "$HIME_SESSION_TMP/launched.log"

new="$old"
for _ in $(seq 50); do
    sleep 0.1
    new="$(listener)"
    [[ "$new" != 0 && "$new" != "$old" ]] && break
done
kill -0 "$old" 2>/dev/null && echo "old daemon: running" || echo "old daemon: gone"
[[ "$new" != 0 && "$new" != "$old" ]] && echo "socket: the new daemon" || echo "socket: not the new daemon"
grep -h "hime: replacing" "$HIME_SESSION_TMP/launched.log" | sed 's/(pid [0-9]*)/(pid N)/'

"$here/hime-client-test" "$@"
