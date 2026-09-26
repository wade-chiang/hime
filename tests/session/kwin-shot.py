#!/usr/bin/env python3
"""Take a screenshot of a KWin session as a binary PPM (what grim -t ppm
gives on wlroots), through KWin's org.kde.KWin.ScreenShot2 D-Bus interface
(allowed with KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1).

Usage: kwin-shot.py OUT.ppm
"""

import os
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

# QImage formats of 32-bit pixels stored as 0xAARRGGBB words (B, G, R, A in
# memory): Format_RGB32, Format_ARGB32, Format_ARGB32_Premultiplied
QIMAGE_XRGB = (4, 5, 6)


def main():
    read_end, write_end = os.pipe()
    fds = Gio.UnixFDList.new()
    index = fds.append(write_end)
    os.close(write_end)

    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    reply, _ = bus.call_with_unix_fd_list_sync(
        "org.kde.KWin", "/org/kde/KWin/ScreenShot2", "org.kde.KWin.ScreenShot2",
        "CaptureWorkspace",
        GLib.Variant("(a{sv}h)", ({"native-resolution": GLib.Variant("b", True)}, index)),
        GLib.VariantType("(a{sv})"), Gio.DBusCallFlags.NONE, 10000, fds, None)
    # KWin closes its end once written
    fds = None
    data = b""
    with os.fdopen(read_end, "rb") as pipe:
        data = pipe.read()

    info = reply.unpack()[0]
    width, height, stride, fmt = (info[k] for k in ("width", "height", "stride", "format"))
    if fmt not in QIMAGE_XRGB:
        sys.exit(f"kwin-shot.py: unsupported QImage format {fmt}")
    rgb = bytearray(width * height * 3)
    for y in range(height):
        row = data[y * stride : y * stride + width * 4]
        rgb[y * width * 3 : (y + 1) * width * 3 : 3] = row[2::4]
        rgb[y * width * 3 + 1 : (y + 1) * width * 3 : 3] = row[1::4]
        rgb[y * width * 3 + 2 : (y + 1) * width * 3 : 3] = row[0::4]
    with open(sys.argv[1], "wb") as out:
        out.write(b"P6\n%d %d\n255\n" % (width, height))
        out.write(rgb)


main()
