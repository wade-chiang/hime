#!/usr/bin/env python3
"""Type keys in a GNOME (Mutter) session through its RemoteDesktop D-Bus
interface, as a keyboard would: Mutter has no virtual keyboard protocol for
wl-type, which runs this instead.  Same keys as wl-type (a character of
the "us" layout, <space> <enter> <bs> <tab> <esc> <shift>, S- and C-
prefixes, @hold KEY).

Usage: rd-type.py KEY...
       rd-type.py --keep: keep a session (a keyboard) until killed: without
       any, GNOME has no keyboard, and windows get no keyboard focus.  It
       types the keys of the other rd-type.py runs, which pass them on
       through $XDG_RUNTIME_DIR/rd-type.fifo: a keyboard that goes away
       resets GNOME's input method focus, dropping what was typed.
"""

import os
import signal

import sys
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

# evdev key codes of the "us" layout: character -> (code, shift)
KEYS = {}
for row, first in (("1234567890-=", 2), ("qwertyuiop[]", 16), ("asdfghjkl;'", 30),
                   ("zxcvbnm,./", 44)):
    for i, c in enumerate(row):
        KEYS[c] = (first + i, False)
for row, first in (('!@#$%^&*()_+', 2), ("QWERTYUIOP{}", 16), ('ASDFGHJKL:"', 30),
                   ("ZXCVBNM<>?", 44)):
    for i, c in enumerate(row):
        KEYS[c] = (first + i, True)
KEYS["`"] = (41, False)
KEYS["~"] = (41, True)
KEYS["\\"] = (43, False)
KEYS["|"] = (43, True)
NAMED = {"<space>": 57, "<enter>": 28, "<bs>": 14, "<tab>": 15, "<esc>": 1, "<shift>": 42}
SHIFT, CTRL = 42, 29

FIFO = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"), "rd-type.fifo")

# Pass the keys on to the rd-type.py --keep of the session, if any
if sys.argv[1:] != ["--keep"] and os.path.exists(FIFO):
    reply = FIFO + "." + str(os.getpid())
    os.mkfifo(reply)
    # the --keep one may have died: do not wait forever for it
    signal.alarm(60)
    with open(FIFO, "w") as fifo:
        fifo.write("\x1f".join([reply] + sys.argv[1:]) + "\n")
    with open(reply) as done:
        done.read()
    os.unlink(reply)
    sys.exit(0)

bus = Gio.bus_get_sync(Gio.BusType.SESSION)


def call(path, interface, method, args=None, reply=None):
    return bus.call_sync("org.gnome.Mutter.RemoteDesktop", path, interface, method, args,
                         GLib.VariantType(reply) if reply else None, Gio.DBusCallFlags.NONE,
                         5000, None)


session = call("/org/gnome/Mutter/RemoteDesktop", "org.gnome.Mutter.RemoteDesktop",
               "CreateSession", None, "(o)").unpack()[0]
SESSION = "org.gnome.Mutter.RemoteDesktop.Session"
call(session, SESSION, "Start")

keep = sys.argv[1:] == ["--keep"]
if keep:
    # the keyboard device appears with its first key; F24 does nothing
    call(session, SESSION, "NotifyKeyboardKeycode", GLib.Variant("(ub)", (194, True)))
    call(session, SESSION, "NotifyKeyboardKeycode", GLib.Variant("(ub)", (194, False)))


def key(code, press):
    call(session, SESSION, "NotifyKeyboardKeycode", GLib.Variant("(ub)", (code, press)))


def parse(token):
    shift = ctrl = False
    while len(token) > 2 and token[:2] in ("S-", "C-"):
        if token[0] == "S":
            shift = True
        else:
            ctrl = True
        token = token[2:]
    if token in NAMED:
        return NAMED[token], shift, ctrl
    if token in KEYS:
        code, level = KEYS[token]
        return code, shift or level, ctrl
    sys.exit(f"rd-type.py: bad key: {token}")


def type_key(token, hold):
    code, shift, ctrl = parse(token)
    if ctrl:
        key(CTRL, True)
    if shift and code != SHIFT:
        key(SHIFT, True)
    key(code, True)
    if hold:
        time.sleep(hold)
    key(code, False)
    if shift and code != SHIFT:
        key(SHIFT, False)
    if ctrl:
        key(CTRL, False)
    # one key at a time, as a person types
    time.sleep(0.05)


def type_keys(args):
    i = 0
    while i < len(args):
        if args[i] == "@hold" and i + 1 < len(args):
            type_key(args[i + 1], 1.0)
            i += 2
        else:
            type_key(args[i], 0)
            i += 1
    time.sleep(0.1)


if keep:
    os.mkfifo(FIFO)
    while True:
        with open(FIFO) as fifo:
            for line in fifo:
                reply, *args = line.rstrip("\n").split("\x1f")
                type_keys(args)
                with open(reply, "w") as done:
                    done.write("done")
else:
    type_keys(sys.argv[1:])
    call(session, SESSION, "Stop")
