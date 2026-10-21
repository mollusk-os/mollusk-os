#!/usr/bin/env python3
"""Generate the looping water-caustics background used by the AnimatedDemo theme.

Writes headerless RGB565 little-endian frames back to back, the format the
RetroFE <animatedImage> component reads (the same thing ffmpeg writes with
"-f rawvideo -pix_fmt rgb565le"). Only the Python standard library is needed.

The pattern is tileable (--tiles repeats it across the frame), and every time
term turns a whole number of times per loop, so the last frame leads straight
back into the first one.

Usage:
    tools/make-caustics-animation.py OUT.rgb565 [--frames 36] [--size 240]
                                     [--tiles 1] [--rgb24 PREVIEW.rgb]
"""

import argparse
import math
import struct
import sys

TAU = 2.0 * math.pi

# Whole turns per loop for each iteration of the caustic function
TIME_TURNS = (-1, 1, -1, 1)
INTENSITY = 0.005

# Pale aqua palette, close to the Classic theme's light grey so its dark text stays readable
TOP_COLOR = (186, 212, 224)
BOTTOM_COLOR = (150, 188, 207)
LIGHT_COLOR = (70, 48, 34)  # added on the bright caustic lines

# 4x4 Bayer matrix for ordered dithering to RGB565 (fixed pattern, so no shimmer between frames)
BAYER4 = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)


def caustic(px, py, t):
    """Brightness (0..1) of a tileable water caustic at (px, py) in [0, TAU)."""
    p_x = px - 250.0
    p_y = py - 250.0
    i_x, i_y = p_x, p_y
    c = 1.0
    for turns in TIME_TURNS:
        tt = t * turns
        i_x, i_y = (p_x + math.cos(tt - i_x) + math.sin(tt + i_y),
                    p_y + math.sin(tt - i_y) + math.cos(tt + i_x))
        s = math.sin(i_x + tt)
        k = math.cos(i_y + tt)
        s = s if abs(s) > 1e-9 else 1e-9
        k = k if abs(k) > 1e-9 else 1e-9
        c += 1.0 / math.hypot(p_x * INTENSITY / s, p_y * INTENSITY / k)
    c /= len(TIME_TURNS)
    c = 1.17 - c ** 1.4
    return min(abs(c) ** 8.0, 1.0)


def to_rgb565(r, g, b, x, y):
    threshold = (BAYER4[y & 3][x & 3] + 0.5) / 16.0
    r5 = min(int(r / 8.0 + threshold), 31)
    g6 = min(int(g / 4.0 + threshold), 63)
    b5 = min(int(b / 8.0 + threshold), 31)
    return (r5 << 11) | (g6 << 5) | b5


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", help="output .rgb565 file")
    parser.add_argument("--frames", type=int, default=36, help="frames per loop (default 36)")
    parser.add_argument("--size", type=int, default=240, help="frame width and height in pixels (default 240)")
    parser.add_argument("--tiles", type=int, default=1, help="pattern repeats across the frame (default 1)")
    parser.add_argument("--rgb24", help="also write undithered RGB24 frames here, for previews")
    args = parser.parse_args()

    size = args.size
    tile = size // args.tiles
    if tile * args.tiles != size:
        sys.exit("--size must be a multiple of --tiles")

    # Vertical gradient, one color per row
    rows = []
    for y in range(size):
        f = y / (size - 1)
        rows.append(tuple(TOP_COLOR[i] + (BOTTOM_COLOR[i] - TOP_COLOR[i]) * f for i in range(3)))

    out565 = open(args.out, "wb")
    out24 = open(args.rgb24, "wb") if args.rgb24 else None

    for n in range(args.frames):
        t = TAU * n / args.frames
        light = [[caustic(TAU * x / tile, TAU * y / tile, t) for x in range(tile)] for y in range(tile)]

        frame565 = bytearray()
        frame24 = bytearray()
        for y in range(size):
            base = rows[y]
            line = light[y % tile]
            for x in range(size):
                v = line[x % tile]
                r = min(base[0] + LIGHT_COLOR[0] * v, 255.0)
                g = min(base[1] + LIGHT_COLOR[1] * v, 255.0)
                b = min(base[2] + LIGHT_COLOR[2] * v, 255.0)
                frame565 += struct.pack("<H", to_rgb565(r, g, b, x, y))
                if out24:
                    frame24 += bytes((int(r), int(g), int(b)))
        out565.write(frame565)
        if out24:
            out24.write(frame24)
        print("frame %d/%d" % (n + 1, args.frames), file=sys.stderr)

    out565.close()
    if out24:
        out24.close()


if __name__ == "__main__":
    main()
