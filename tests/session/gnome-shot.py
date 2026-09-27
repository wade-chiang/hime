#!/usr/bin/env python3
"""Take a screenshot of a GNOME session as a binary PPM (what grim -t ppm
gives on wlroots), through GNOME Shell's org.gnome.Shell.Screenshot D-Bus
interface (allowed in unsafe mode, gnome-shell --unsafe-mode).

Usage: gnome-shot.py OUT.ppm
"""

import os
import sys

import gi

gi.require_version("Gio", "2.0")
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import GdkPixbuf, Gio, GLib  # noqa: E402

png = sys.argv[1] + ".png"
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
bus.call_sync("org.gnome.Shell.Screenshot", "/org/gnome/Shell/Screenshot",
              "org.gnome.Shell.Screenshot", "Screenshot",
              GLib.Variant("(bbs)", (False, False, png)), GLib.VariantType("(bs)"),
              Gio.DBusCallFlags.NONE, 10000, None)
pixbuf = GdkPixbuf.Pixbuf.new_from_file(png)
os.unlink(png)
width, height = pixbuf.get_width(), pixbuf.get_height()
channels, stride = pixbuf.get_n_channels(), pixbuf.get_rowstride()
pixels = pixbuf.get_pixels()
rgb = bytearray()
for y in range(height):
    row = pixels[y * stride : y * stride + width * channels]
    if channels == 3:
        rgb += row
    else:
        rgb += bytes(b for i, b in enumerate(row) if i % 4 != 3)
with open(sys.argv[1], "wb") as out:
    out.write(b"P6\n%d %d\n255\n" % (width, height))
    out.write(rgb)
