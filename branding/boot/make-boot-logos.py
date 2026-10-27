#!/usr/bin/env python3
"""Make the kernel boot logos from the Mollusk OS logo.

The FunKey's kernel shows a 240x240 logo while it boots, made at build time from a plain
PPM file with at most 224 colors (drivers/video/logo/ in the kernel sources). This writes,
next to this script:

  logo_funkey_clut224.ppm             the normal boot: the logo, centred on black
  logo_funkey_recovery_clut224.ppm    Recovery: the logo, with the tools icon below it
  *-2x.png                            previews, twice the size

from ../logo/logo-stacked-dark.svg, and recovery-tools.png (the wrench and screwdriver of
the FunKey OS Recovery logo). The kernel builds copy the PPM files into the kernel sources
(see FunKey/external.mk and Recovery/external.mk).

Needs rsvg-convert (librsvg) and Pillow. Run from anywhere: python3 make-boot-logos.py
"""

import os
import subprocess
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
LOGO = os.path.join(HERE, "..", "logo", "logo-stacked-dark.svg")
TOOLS_ICON = os.path.join(HERE, "recovery-tools.png")
SIZE = 240


def render(svg_path, box):
    """The SVG rendered to fit a box x box square, keeping its proportions"""
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "logo.png")
        subprocess.run(["rsvg-convert", "-a", "-w", str(box), "-h", str(box), "-o", png, svg_path], check=True)
        return Image.open(png).convert("RGBA")


def to_224_colors(img):
    q = img.convert("RGB").quantize(224, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    return q.convert("RGB")


def write_ppm(img, path):
    """A plain (P3) PPM, like the kernel's own logo files"""
    w, h = img.size
    px = list(img.getdata())
    with open(path, "w", newline="\n") as f:
        f.write(f"P3\n{w} {h}\n255\n")
        for y in range(h):
            row = px[y * w:(y + 1) * w]
            f.write(" ".join(f"{r} {g} {b}" for r, g, b in row) + "\n")


def save(img, name):
    img = to_224_colors(img)
    colors = len(img.getcolors(SIZE * SIZE))
    assert colors <= 224, f"{name}: {colors} colors"
    write_ppm(img, os.path.join(HERE, name + ".ppm"))
    img.resize((SIZE * 2, SIZE * 2), Image.Resampling.NEAREST).save(os.path.join(HERE, name + "-2x.png"))
    print(f"{name}.ppm: {colors} colors")


def main():
    # normal boot: the stacked logo, fitted to 196x196 and centred
    logo = render(LOGO, 196)
    boot = Image.new("RGBA", (SIZE, SIZE), "#000000")
    boot.alpha_composite(logo, ((SIZE - logo.width) // 2, (SIZE - logo.height) // 2))
    save(boot, "logo_funkey_clut224")

    # Recovery: a smaller logo above the tools icon, the pair centred
    logo = render(LOGO, 172)
    tools = Image.open(TOOLS_ICON).convert("RGBA")
    gap = 6
    top = (SIZE - (logo.height + gap + tools.height)) // 2
    rec = Image.new("RGBA", (SIZE, SIZE), "#000000")
    rec.alpha_composite(logo, ((SIZE - logo.width) // 2, top))
    rec.alpha_composite(tools, ((SIZE - tools.width) // 2, top + logo.height + gap))
    save(rec, "logo_funkey_recovery_clut224")


if __name__ == "__main__":
    main()
