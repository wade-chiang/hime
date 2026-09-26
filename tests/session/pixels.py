#!/usr/bin/env python3
"""Report which points of a screenshot show the background.

Usage: pixels.py SHOT.ppm REF_X,REF_Y X,Y...
  SHOT.ppm is a binary PPM (grim -t ppm).  The pixel at REF_X,REF_Y is taken
  as the background.  Prints "X,Y background" or "X,Y window" for each point.
"""

import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            pos = data.index(b"\n", pos)
            continue
        end = pos
        while not data[end : end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or int(fields[3]) != 255:
        sys.exit("not an 8-bit binary PPM")
    return int(fields[1]), int(fields[2]), data[pos + 1 :]


def main():
    width, height, pixels = read_ppm(sys.argv[1])
    def pixel(point):
        x, y = (int(v) for v in point.split(","))
        if not (0 <= x < width and 0 <= y < height):
            sys.exit(f"{point} is outside the {width}x{height} screenshot")
        offset = (y * width + x) * 3
        return pixels[offset : offset + 3]

    background = pixel(sys.argv[2])
    for point in sys.argv[3:]:
        print(point, "background" if pixel(point) == background else "window")


main()
