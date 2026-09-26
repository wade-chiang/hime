#!/usr/bin/env bash
#
# Inside a session (run-session.sh): print the GDK backend the daemon chose.

grep '^hime: using the' "$HIME_SESSION_TMP/hime.log"
