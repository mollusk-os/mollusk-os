#!/usr/bin/env python3
"""Builds the three koi theme drafts for Mollusk OS's RetroFE launcher.

For each draft this writes a complete theme folder (layout.xml, splash.xml, the
animation, images, icons, font, sounds, Credits.txt), mockups of the main menu and
the game list, animated previews (APNG and GIF) and a text-contrast report.

The layout is the stock FunKey theme's layout.xml (the theme every FunKey ships
with), adapted: its background images are replaced by the <animatedImage> koi
pond, and its colors, font, icons and bars are each draft's own. Its menus and
animations are kept as they are.

Needs (all already on the build PC):
  - tools/koi_frames.exe (build.cmd) for the frames, icons and contrast check
  - WSL Debian with ImageMagick 7 and ffmpeg (mockups and previews)
  - the mollusk-os checkout in WSL, read only (stock layout, art, fonts, sounds)

Usage: python make_themes.py [lily] [glass] [night]   (default: all three)
"""

import json
import os
import re
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DRAFTS = os.path.dirname(HERE)                      # ...\themes\koi-drafts
EXE = os.path.join(HERE, "koi_frames.exe")
WSL_ROOT = r"\\wsl.localhost\Debian\root\src\mollusk-os"
GAMES = os.path.join(WSL_ROOT, r"FunKey\board\funkey\rootfs-overlay\usr\games")
BUILD = os.path.join(DRAFTS, "build")               # intermediate files (frames, mockups)

FPS_FRAMES = 36

THEMES = {
    "lily": dict(
        folder="KoiLilyPond", title="Koi Lily Pond", anim="lilypond.rgb565", fps=9,
        font="Cabin-Bold-TTF.ttf", font_src=r"layouts\Flat\Cabin-Bold-TTF.ttf",
        icon="f3eee6", halo="03181b", halo_a=0.6,
        title_txt="f3eee6", band="062427", band_a=0.62,
        batt="e2eeea", chip="062427", chip_a=0.55,
        panel="072528", panel_a=0.74, artcol="9cc4b8", artcol_a=0.85,
        accent="b8381a", accent_a=0.92, sel_txt="f8f1e8",
        row_txt="d6e6e1", line="6fae69", line_k=0.4,
        status="041a1c", status_a=1.0, status_txt="e2eeea",
        card=None,
        summary="Calm top-down pond by day: deep teal water, lily pads with pink water lilies, sakura petals, "
                "ripples and sunlight caustics. Kohaku and sanke circle the pads; ogon, platinum and chagoi pairs cross.",
        palette="Firefox add-on, Day look",
    ),
    "glass": dict(
        folder="KoiGlass", title="Koi Glass", anim="glass.rgb565", fps=9,
        font="OpenSans-Bold.ttf", font_src=r"layouts\FunKey\OpenSans-Bold.ttf",
        icon="17393d", halo="ffffff", halo_a=0.55,
        title_txt="12282c", band="ffffff", band_a=0.55,
        batt="12282c", chip="ffffff", chip_a=0.5,
        panel="f4faf8", panel_a=0.6, artcol=None, artcol_a=0,
        accent="17666b", accent_a=0.92, sel_txt="ffffff",
        row_txt="12282c", line="12282c", line_k=0.18,
        status="e8f2f0", status_a=1.0, status_txt="12282c",
        card=dict(w=148, h=140, r=18, fill="ffffff", fill_a=0.28, rim="ffffff", rim_a=0.85),
        # the selection bar starts after the box art: the stock fallback art is black and
        # would vanish on the dark teal bar
        accent_x=60,
        summary="Glass Wings style: a sunlit, shallow pond seen through light frosted acrylic, the koi softly blurred "
                "under the glass. Dark text on light glass panels; the selected system sits on a glass card.",
        palette="Glass Wings koi scene, light acrylic",
    ),
    "night": dict(
        folder="KoiMoonlight", title="Koi Moonlight", anim="moonlight.rgb565", fps=8,
        font="Roboto-Bold.ttf", font_src=r"layouts\FunKey\Roboto-Bold.ttf",
        icon="dfe7f7", halo="03081a", halo_a=0.65,
        title_txt="ffcf86", band="050d20", band_a=0.66,
        batt="dfe7f7", chip="050d20", chip_a=0.55,
        panel="071226", panel_a=0.76, artcol="8ea3c9", artcol_a=0.8,
        accent="ffc46b", accent_a=0.94, sel_txt="0a1630",
        row_txt="cfd9ee", line="a8bce8", line_k=0.3,
        status="040a18", status_a=1.0, status_txt="dfe7f7",
        card=None,
        summary="Moonlit pond: navy water with the moon's reflection breaking into slivers, reflected stars, two paper "
                "lanterns whose light warms the koi swimming beneath, fireflies, and closed water lilies.",
        palette="Firefox add-on, Night look",
    ),
}

# ----------------------------------------------------------------------------
# small helpers
# ----------------------------------------------------------------------------


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    a, b = rgb(a), rgb(b)
    return "%02x%02x%02x" % tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def write_png(path, w, h, pixels):
    """pixels: list of (r, g, b, a) rows, straight alpha 0..255."""
    raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in pixels)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def solid_png(path, color):
    """A 1-pixel RGB image (no alpha channel). RetroFE turns images that aren't 32 bpp into
    RGB surfaces, on which a component's alpha works; on a 32 bpp RGBA image SDL 1.2
    ignores the component's alpha, so a translucent bar would be drawn opaque."""
    r, g, b = rgb(color)
    raw = b"\x00" + bytes((r, g, b))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def rounded_png(path, w, h, radius, color, alpha, rim=None, rim_a=0.0, fade_x=0, sheen=0.0):
    """A rounded rectangle (anti-aliased), optional 1px rim, horizontal edge fade and top sheen."""
    r, g, b = rgb(color)
    rr, rg, rb = rgb(rim) if rim else (0, 0, 0)
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            # signed distance to the rounded rect (pixel centers)
            px, py = x + 0.5, y + 0.5
            qx = abs(px - w / 2) - (w / 2 - radius)
            qy = abs(py - h / 2) - (h / 2 - radius)
            out = (max(qx, 0) ** 2 + max(qy, 0) ** 2) ** 0.5 + min(max(qx, qy), 0) - radius
            cover = min(1.0, max(0.0, 0.5 - out))
            a = alpha * cover
            if fade_x:
                a *= min(1.0, min(px, w - px) / fade_x)
            cr, cg, cb = r, g, b
            if sheen:
                k = sheen * max(0.0, 1.0 - py / (h * 0.45))
                cr, cg, cb = (round(c + (255 - c) * k) for c in (cr, cg, cb))
                a = min(1.0, a + 0.15 * k * cover)
            if rim:
                ring = min(1.0, max(0.0, 0.5 - abs(out + 0.5))) * rim_a
                if ring > 0:
                    na = ring + a * (1 - ring)
                    if na > 0:
                        cr = round((rr * ring + cr * a * (1 - ring)) / na)
                        cg = round((rg * ring + cg * a * (1 - ring)) / na)
                        cb = round((rb * ring + cb * a * (1 - ring)) / na)
                    a = na
            row.append((cr, cg, cb, round(a * 255)))
        rows.append(row)
    write_png(path, w, h, rows)


def gradient_png(path, w, color, alpha):
    r, g, b = rgb(color)
    write_png(path, w, 1, [[(r, g, b, round(255 * alpha * (x + 0.5) / w)) for x in range(w)]])


def wsl(script):
    """Runs a bash script in WSL Debian (written to a file; inline quoting through wsl.exe breaks)."""
    os.makedirs(BUILD, exist_ok=True)
    path = os.path.join(BUILD, "_step.sh")
    with open(path, "w", newline="\n") as f:
        f.write("set -e\n" + script)
    r = subprocess.run(["wsl.exe", "-d", "Debian", "--", "bash", to_wsl(path)], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("WSL step failed:\n" + script[-1500:] + "\n" + r.stderr[-1500:])
    return r.stdout


def to_wsl(p):
    p = os.path.abspath(p)
    if p.startswith("\\\\wsl.localhost\\Debian"):
        return p[len("\\\\wsl.localhost\\Debian"):].replace("\\", "/")
    return "/mnt/" + p[0].lower() + p[2:].replace("\\", "/")


def q(p):
    return "'" + to_wsl(p).replace("'", "'\\''") + "'"


# ----------------------------------------------------------------------------
# layout.xml: the stock FunKey layout, adapted
# ----------------------------------------------------------------------------


def sanitize_funkey_xml(text):
    """Makes the stock layout well-formed without changing what RetroFE reads.

    RetroFE's parser (rapidxml, non-validating) closes the current element at any
    closing tag, whatever its name (the stock file has e.g. <onGameExit>...</onGameEnter>),
    and skips comments up to "-->". This rewrites every closing tag to the name of the
    element it actually closes, drops "--" runs inside comments and stray text.
    """
    out, stack, pos = [], [], 0
    token = re.compile(r"<!--(.*?)-->|<(/?)([A-Za-z_][\w.-]*)([^>]*?)(/?)>", re.S)
    for m in token.finditer(text):
        between = text[pos:m.start()]
        out.append(between if between.strip() == "" else re.sub(r"[^\s]", "", between))
        pos = m.end()
        if m.group(1) is not None:
            body = re.sub(r"-{2,}", "-", m.group(1)).strip("-")
            out.append("<!--" + body + "-->")
        elif m.group(2) == "/":
            name = stack.pop()
            out.append("</" + name + ">")
        else:
            attrs = m.group(4)
            if m.group(5) == "/":
                out.append("<" + m.group(3) + attrs + "/>")
            else:
                stack.append(m.group(3))
                out.append("<" + m.group(3) + attrs + ">")
    out.append(text[pos:])
    return "".join(out)


def sub(parent, tag, **attrs):
    e = ET.SubElement(parent, tag)
    for k, v in attrs.items():
        e.set(k.rstrip("_"), str(v))
    return e


def alpha_events(e, events):
    """events: list of (tag, menuIndex or None, alpha, duration)."""
    for tag, menu, to, dur in events:
        ev = sub(e, tag, **({"menuIndex": menu} if menu is not None else {}))
        s = sub(ev, "set", duration=dur)
        sub(s, "animate", type="alpha", to=to)


def fmt(a):
    return ("%.2f" % a).rstrip("0").rstrip(".")


def build_layout(t):
    src = open(os.path.join(GAMES, r"layouts\FunKey\layout.xml"), encoding="utf-8").read()
    clean = sanitize_funkey_xml(src)
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    root = ET.fromstring(clean, parser=parser)
    root.set("font", t["font"])
    root.set("fontColor", t["row_txt"])

    P, H, S = fmt(t["panel_a"]), fmt(t["accent_a"]), fmt(t["status_a"])
    kids = list(root)
    batteries = 0
    new_children = []
    stale = ("Background Color", "Background Image", "images/test.png", "TO REMOVE", "background_1px.png")
    for e in kids:
        tag = e.tag if isinstance(e.tag, str) else None
        if tag is None and (any(s in (e.text or "") for s in stale) or "<" in (e.text or "")):
            continue  # stock comments about the replaced backgrounds, and commented-out components
        src_attr = e.get("src", "") if tag else ""
        if tag == "image" and src_attr in ("images/white_1px.png", "images/background_purple.png"):
            if src_attr == "images/white_1px.png":
                new_children += background_elements(t)
            continue
        if tag == "menu" and e.get("imageType") == "device_W140":
            e.set("imageType", "koi_icon")
            d = e.find("itemDefaults")
            d.set("width", "120")
            d.set("alpha", "0.6")
            d.set("yOffset", "-12")
            d.set("fontColor", t["title_txt"])
            d.set("fontSize", "16")
            d.set("loadFontSize", "16")
            for it in e.findall("item"):
                if it.get("xOffset") == "-170":
                    it.set("xOffset", "-150")
                if it.get("xOffset") == "170":
                    it.set("xOffset", "150")
                for an in it.iter("animate"):
                    if an.get("to") == "0.8":
                        an.set("to", "0.6")
        elif tag == "reloadableText" and e.get("textFormat") == "uppercase" and e.get("type") == "title":
            # main menu title, on its band
            band = ET.Element("image", {"src": "images/title_band.png", "x": "center", "xOrigin": "center", "y": "bottom",
                                        "yOrigin": "center", "yOffset": "-28", "alpha": "0", "layer": "13"})
            alpha_events(band, [("onEnter", None, "1", ".3"), ("onExit", None, "0", ".3"),
                                ("onMenuEnter", "0", "1", ".3"), ("onMenuExit", "0", "0", ".3"),
                                ("onGameEnter", "0", "0", ".3"), ("onGameExit", "0", "1", ".3")])
            new_children.append(ET.Comment(" Band behind the system name "))
            new_children.append(band)
            e.set("yOffset", "-28")
            e.set("fontSize", "16")
            e.set("loadFontSize", "16")
            e.set("fontColor", t["title_txt"])
        elif tag == "battery":
            batteries += 1
            if batteries == 1:
                chip = ET.Element("image", {"src": "images/battery_chip.png", "x": "right", "xOrigin": "right", "xOffset": "-2",
                                            "y": "top", "yOrigin": "center", "yOffset": "13", "alpha": "0", "layer": "13"})
                alpha_events(chip, [("onEnter", None, "1", ".3"), ("onExit", None, "0", ".3"),
                                    ("onGameEnter", "1", "0", ".3"), ("onGameExit", "1", "1", ".3")])
                new_children.append(ET.Comment(" Backing for the battery icon "))
                new_children.append(chip)
                e.set("fontColor", t["batt"])
            else:
                e.set("fontColor", t["status_txt"])
        elif tag == "image" and src_attr == "images/purple_corrected_1px.png":
            e.set("src", "images/accent_1px.png")
            if t.get("accent_x"):
                e.set("xOffset", str(t["accent_x"]))
                e.set("width", str(240 - t["accent_x"]))
            for an in e.iter("animate"):
                if an.get("to") == "1":
                    an.set("to", H)
        elif tag == "image" and src_attr == "images/gray_1px.png":
            if e.get("height") == "25":
                e.set("src", "images/status_1px.png")
                for an in e.iter("animate"):
                    if an.get("to") == "1":
                        an.set("to", S)
            else:
                e.set("src", "images/line_1px.png")
        elif tag == "image" and src_attr == "images/gradient.png":
            e.set("src", "images/gradient_right.png")
        elif tag == "reloadableScrollingText":
            e.set("fontColor", t["sel_txt"])
        elif tag == "reloadableText" and e.get("type") == "title":
            fc = e.get("fontColor", "").lower()
            if e.get("selectedOffset") == "-1" or fc == "ffffffff":
                e.set("fontColor", t["sel_txt"])
            else:
                e.set("fontColor", t["row_txt"])
        elif tag == "reloadableText" and e.get("type") == "collectionName":
            e.set("fontColor", t["status_txt"])
        new_children.append(e)
    for e in kids:
        root.remove(e)
    for e in new_children:
        root.append(e)
    header = ET.Comment(" %s: a Mollusk OS koi theme. The stock FunKey layout with an animated koi pond "
                        "behind it; see Credits.txt. " % t["title"])
    root.insert(0, header)
    ET.indent(root, "    ")
    xml = ET.tostring(root, encoding="unicode")
    ET.fromstring(xml)  # well-formed
    return xml + "\n"


def background_elements(t):
    els = [ET.Comment(" Animated koi pond: %d frames of 240x240 RGB565 in animations/%s (%.1f MB), %d fps, a %.1f s loop. "
                      "idleFps 0 pauses it after animationIdleTimeout seconds without a button press. "
                      % (FPS_FRAMES, t["anim"], FPS_FRAMES * 115200 / 1e6, t["fps"], FPS_FRAMES / t["fps"]))]
    els.append(ET.Element("animatedImage", {"src": "animations/" + t["anim"], "frameWidth": "240", "frameHeight": "240",
                                            "fps": str(t["fps"]), "idleFps": "0", "x": "0", "y": "0", "layer": "0"}))
    if t["card"]:
        c = t["card"]
        card = ET.Element("image", {"src": "images/card.png", "x": "center", "xOrigin": "center", "y": "center",
                                    "yOrigin": "center", "yOffset": "-12", "alpha": "0", "layer": "3"})
        alpha_events(card, [("onEnter", None, "1", ".3"), ("onExit", None, "0", ".3"),
                            ("onMenuEnter", "0", "1", ".3"), ("onMenuExit", "0", "0", ".2"),
                            ("onGameEnter", "0", "0", ".3"), ("onGameExit", "0", "1", ".3")])
        els.append(ET.Comment(" Glass card behind the selected system "))
        els.append(card)
    panel = ET.Element("image", {"src": "images/panel_1px.png", "x": "0", "y": "24", "width": "stretch", "height": "216",
                                 "alpha": "0", "layer": "10"})
    alpha_events(panel, [("onExit", None, "0", ".3"), ("onMenuEnter", "1", fmt(t["panel_a"]), ".3"),
                         ("onMenuExit", "1", "0", ".3"), ("onGameEnter", "1", "0", ".3"),
                         ("onGameExit", "1", fmt(t["panel_a"]), ".3")])
    els.append(ET.Comment(" Game list: a translucent panel over the pond "))
    els.append(panel)
    if t["artcol"]:
        col = ET.Element("image", {"src": "images/artcol_1px.png", "x": "0", "y": "24", "width": "60", "height": "216",
                                   "alpha": "0", "layer": "11"})
        alpha_events(col, [("onExit", None, "0", ".3"), ("onMenuEnter", "1", fmt(t["artcol_a"]), ".3"),
                           ("onMenuExit", "1", "0", ".3"), ("onGameEnter", "1", "0", ".3"),
                           ("onGameExit", "1", fmt(t["artcol_a"]), ".3")])
        els.append(ET.Comment(" Light column behind the box art, so the dark fallback pictures show "))
        els.append(col)
    return els


def build_splash(t):
    return """<layout width="240" height="240" font="{font}" loadFontSize="16" fontColor="{fg}">
    <!-- {title} loading screen: the pond's first frame, with "Loading..." on the title band -->
    <image src="images/splash.png" x="0" y="0" layer="0"/>
    <image src="images/title_band.png" x="center" xOrigin="center" y="bottom" yOrigin="center" yOffset="-28" layer="1"/>
    <text value="Loading..." x="center" y="bottom" xOrigin="center" yOrigin="center" yOffset="-28" fontSize="16" layer="3">
        <onIdle>
            <set duration=".4">
                <animate type="alpha" from="1" to=".5" algorithm="easeinquadratic"/>
            </set>
            <set duration=".4">
                <animate type="alpha" from=".5" to="1" algorithm="easeinquadratic"/>
            </set>
        </onIdle>
    </text>
</layout>
""".format(font=t["font"], fg=t["title_txt"], title=t["title"])


def build_credits(t):
    return """{title} - a Mollusk OS koi theme (generated by tools/koi-themes)

Layout: the stock FunKey theme's layout.xml from FunKey-OS, with an animated background
(<animatedImage>), new colors, bars and icons.

Koi: drawn by the koi renderer of Glass Wings (windhawk/scenes/koi_pond.h),
Copyright (c) 2026 HourlySword, MIT License.
Water, lily pads, water lilies, ripples{extra}: colors and shapes after
Koi Pond Animated (Day/Night Cycle), Copyright (c) 2026 HourlySword, CC BY-NC-SA 4.0.
Water caustics: the function of Mollusk OS's tools/make-caustics-animation.py.

System icons: the FunKey-OS theme device pictures, recolored, with a soft halo;
the arcade, PICO-8 and Pokemon Mini icons are new.
Font: {font_name}.
Sounds: the Classic theme's sounds (theme by TimmyB).
  Highlight.wav by orginaljun - orginaljun.deviantart.com
  http://www.freesound.org/people/orginaljun/sounds/157871/
""".format(title=t["title"],
           extra=", lanterns, fireflies and the moon's reflection" if t["folder"] == "KoiMoonlight" else "",
           font_name={"Cabin-Bold-TTF.ttf": "Cabin Bold by Pablo Impallari (SIL Open Font License 1.1)",
                      "OpenSans-Bold.ttf": "Open Sans Bold by Steve Matteson (Apache License 2.0)",
                      "Roboto-Bold.ttf": "Roboto Bold by Christian Robertson (Apache License 2.0)"}[t["font"]])


# ----------------------------------------------------------------------------
# theme folder
# ----------------------------------------------------------------------------


def build_theme(key):
    t = THEMES[key]
    out = os.path.join(DRAFTS, t["folder"])
    work = os.path.join(BUILD, key)
    frames = os.path.join(work, "frames")
    if os.path.isdir(out):
        shutil.rmtree(out)
    for d in ("animations", "images", "sounds", "collections"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    os.makedirs(work, exist_ok=True)

    print("[%s] rendering frames" % key)
    subprocess.run([EXE, "render", key, work], check=True, stdout=subprocess.DEVNULL)
    shutil.copyfile(os.path.join(work, "anim.rgb565"), os.path.join(out, "animations", t["anim"]))
    shutil.copyfile(os.path.join(frames, "f000.png"), os.path.join(out, "images", "splash.png"))

    print("[%s] images and icons" % key)
    img = os.path.join(out, "images")
    solid_png(os.path.join(img, "panel_1px.png"), t["panel"])
    solid_png(os.path.join(img, "accent_1px.png"), t["accent"])
    solid_png(os.path.join(img, "status_1px.png"), t["status"])
    # separators are drawn opaque by the stock animations: pre-blend them with the panel
    solid_png(os.path.join(img, "line_1px.png"), mix(t["panel"], t["line"], t["line_k"]))
    if t["artcol"]:
        solid_png(os.path.join(img, "artcol_1px.png"), t["artcol"])
    gradient_png(os.path.join(img, "gradient_right.png"), 12, t["panel"], t["panel_a"])
    rounded_png(os.path.join(img, "title_band.png"), 240, 30, 6, t["band"], t["band_a"], fade_x=46)
    rounded_png(os.path.join(img, "battery_chip.png"), 42, 22, 11, t["chip"], t["chip_a"])
    if t["card"]:
        c = t["card"]
        rounded_png(os.path.join(img, "card.png"), c["w"], c["h"], c["r"], c["fill"], c["fill_a"], rim=c["rim"],
                    rim_a=c["rim_a"], sheen=0.35)
    devices = os.path.join(BUILD, "devices")
    subprocess.run([EXE, "icons", devices, os.path.join(out, "collections"), t["icon"], t["halo"], str(t["halo_a"]),
                    "120", "koi_icon.png"], check=True, stdout=subprocess.DEVNULL)

    shutil.copyfile(os.path.join(GAMES, t["font_src"]), os.path.join(out, t["font"]))
    for w in ("load", "unload", "highlight", "select"):
        shutil.copyfile(os.path.join(GAMES, r"layouts\Classic\sounds", w + ".wav"), os.path.join(out, "sounds", w + ".wav"))
    with open(os.path.join(out, "layout.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(build_layout(t))
    with open(os.path.join(out, "splash.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(build_splash(t))
    with open(os.path.join(out, "Credits.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(build_credits(t))
    return out, work


def copy_devices():
    """The stock device pictures, one folder per system of the main menu."""
    dst = os.path.join(BUILD, "devices")
    if os.path.isdir(dst):
        return
    menu = open(os.path.join(GAMES, r"collections\Main\menu.txt"), encoding="utf-8").read().split("\n")
    for sys_name in (s.strip() for s in menu):
        if not sys_name:
            continue
        d = os.path.join(dst, sys_name)
        os.makedirs(d, exist_ok=True)
        for cand in (os.path.join(GAMES, "layouts", "FunKey", "collections", sys_name, "system_artwork", "device.png"),
                     os.path.join(GAMES, "collections", sys_name, "system_artwork", "device.png"),
                     os.path.join(GAMES, "layouts", "FunKey", "collections", sys_name, "system_artwork", "device_W140.png")):
            if os.path.isfile(cand):
                shutil.copyfile(cand, os.path.join(d, os.path.basename(cand)))
                break


# ----------------------------------------------------------------------------
# mockups, previews, contrast
# ----------------------------------------------------------------------------

GAMES_LIST = ["Super Mario World", "Donkey Kong Country", "F-Zero", "Super Metroid"]


def mockups(key, out, work):
    """Overlays of the main menu and the game list as RetroFE would draw them (positions from layout.xml)."""
    t = THEMES[key]
    font = os.path.join(out, t["font"])
    icons = os.path.join(out, "collections")
    fallback = os.path.join(GAMES, r"collections\SNES\system_artwork\fallback.png")
    m = os.path.join(work, "mock")
    os.makedirs(m, exist_ok=True)
    bat = "#" + t["batt"]
    sbat = "#" + t["status_txt"]

    def label(name, text, size, color):
        return "magick -background none -fill '#{c}' -font {f} -pointsize {s} label:{t} {o}\n".format(
            c=color, f=q(font), s=size, t="'" + text.replace("'", "'\\''") + "'", o=q(os.path.join(m, name)))

    sh = "cd " + q(m) + "\n"
    # text pieces
    sh += label("t_title.png", "SNES", 16, t["title_txt"])
    sh += label("t_status.png", "SNES", 15, t["status_txt"])
    for i, g in enumerate(GAMES_LIST):
        sh += label("t_row%d.png" % i, g, 14, t["sel_txt"] if i == 0 else t["row_txt"])
    # battery icon (RetroFE's 34x16 bitmap: outline, nub, fill bar), ~70 % full
    for name, col in (("bat_main.png", bat), ("bat_status.png", sbat)):
        sh += ("magick -size 34x16 xc:none -fill '{c}' -draw 'rectangle 2,2 29,3' -draw 'rectangle 2,12 29,13' "
               "-draw 'rectangle 2,2 3,13' -draw 'rectangle 28,2 29,13' -draw 'rectangle 30,6 31,9' "
               "-draw 'rectangle 5,5 20,10' {o}\n").format(c=col, o=q(os.path.join(m, name)))
    sh += "magick {f} -resize 60x60 -background none -gravity center -extent 60x60 fb.png\n".format(f=q(fallback))
    sh += "magick -size 240x240 xc:none -type TrueColorAlpha PNG32:canvas.png\n"
    img = os.path.join(out, "images")
    ic = lambda s: q(os.path.join(icons, s, "system_artwork", "koi_icon.png"))
    # ---- main menu: under (no text) and full
    main = "magick canvas.png"
    if t["card"]:
        c = t["card"]
        main += " {card} -geometry +{x}+{y} -composite".format(card=q(os.path.join(img, "card.png")),
                                                              x=120 - c["w"] // 2, y=108 - c["h"] // 2)
    # neighbours at full strength: RetroFE ignores a component's alpha on RGBA images (SDL 1.2)
    for sysn, cx, a in (("NES", -30, 1.0), ("SNES", 120, 1.0), ("Game Gear", 270, 1.0)):
        main += " \\( {i} -channel A -evaluate multiply {a} +channel \\) -geometry {g} -composite".format(
            i=ic(sysn), a=a, g="%+d%+d" % (cx - 60, 108 - 60))
    main += " {b} -geometry +0+197 -composite".format(b=q(os.path.join(img, "title_band.png")))
    main += " {c} -geometry +196+2 -composite".format(c=q(os.path.join(img, "battery_chip.png")))
    sh += main + " PNG32:under_main.png\n"
    # ---- game list
    lst = "magick canvas.png"
    lst += " \\( {p} -scale 240x216! -alpha set -channel A -evaluate set {a}% +channel \\) -geometry +0+24 -composite".format(
        p=q(os.path.join(img, "panel_1px.png")), a=round(t["panel_a"] * 100))
    if t["artcol"]:
        lst += " \\( {p} -scale 60x216! -alpha set -channel A -evaluate set {a}% +channel \\) -geometry +0+24 -composite".format(
            p=q(os.path.join(img, "artcol_1px.png")), a=round(t["artcol_a"] * 100))
    ax = t.get("accent_x", 0)
    lst += " \\( {p} -scale {w}x60! -alpha set -channel A -evaluate set {a}% +channel \\) -geometry +{x}+24 -composite".format(
        p=q(os.path.join(img, "accent_1px.png")), a=round(t["accent_a"] * 100), w=240 - ax, x=ax)
    for y in (84, 144, 204):
        lst += " \\( {p} -scale 240x1! \\) -geometry +0+{y} -composite".format(p=q(os.path.join(img, "line_1px.png")), y=y)
    for i in range(4):
        lst += " fb.png -geometry +0+%d -composite" % (24 + 60 * i)
    lst += " \\( {g} -scale 12x156! \\) -geometry +228+84 -composite".format(g=q(os.path.join(img, "gradient_right.png")))
    lst += " \\( {p} -scale 240x25! -alpha set -channel A -evaluate set {a}% +channel \\) -geometry +0+0 -composite".format(
        p=q(os.path.join(img, "status_1px.png")), a=round(t["status_a"] * 100))
    # (the battery chip, layer 13, is under the opaque status bar, layer 16, in the list)
    sh += lst + " PNG32:under_list.png\n"
    # text sizes, to center each text on its y position as RetroFE does
    sh += "for f in t_title.png t_status.png t_row0.png t_row1.png t_row2.png t_row3.png; do echo \"$f $(magick identify -format '%w %h' $f)\"; done\n"
    dims = {}
    for line in wsl(sh).splitlines():
        p = line.split()
        if len(p) == 3:
            dims[p[0]] = (int(p[1]), int(p[2]))
    ty = 212 - dims["t_title.png"][1] // 2
    sy = 10 - dims["t_status.png"][1] // 2
    rows_y = [54 + 60 * i - dims["t_row%d.png" % i][1] // 2 for i in range(4)]
    final = ("cd " + q(m) + "\n" +
             "magick under_main.png bat_main.png -geometry +201+5 -composite t_title.png -gravity north -geometry +0+%d -composite PNG32:main.png\n" % ty +
             "magick under_list.png bat_status.png -geometry +201+5 -composite t_status.png -geometry +8+%d -composite" % sy +
             "".join(" t_row%d.png -geometry +70+%d -composite" % (i, rows_y[i]) for i in range(4)) + " PNG32:list.png\n")
    wsl(final)
    # text boxes for the contrast check (the text's own box, as RetroFE lays it out)
    tw = dims["t_title.png"][0]
    boxes_main = ["%d %d %d %d %s system name" % (120 - tw // 2, ty, 120 + (tw + 1) // 2, ty + dims["t_title.png"][1], t["title_txt"]),
                  "%d %d %d %d %s battery icon (main)" % (203, 7, 232, 19, t["batt"])]
    boxes_list = ["%d %d %d %d %s collection name" % (8, sy, 8 + dims["t_status.png"][0], sy + dims["t_status.png"][1], t["status_txt"]),
                  "%d %d %d %d %s battery icon (list)" % (203, 7, 232, 19, t["status_txt"])]
    for i in range(4):
        w, h = dims["t_row%d.png" % i]
        boxes_list.append("%d %d %d %d %s %s" % (70, rows_y[i], min(240, 70 + w), min(240, rows_y[i] + h),
                                                  t["sel_txt"] if i == 0 else t["row_txt"],
                                                  "selected game" if i == 0 else "game row %d" % i))
    with open(os.path.join(m, "boxes_main.txt"), "w") as f:
        f.write("\n".join(boxes_main) + "\n")
    with open(os.path.join(m, "boxes_list.txt"), "w") as f:
        f.write("\n".join(boxes_list) + "\n")
    return m


def contrast(key, work, m):
    frames = os.path.join(work, "frames")
    res = {}
    for screen in ("main", "list"):
        outp = os.path.join(m, "contrast_%s.txt" % screen)
        subprocess.run([EXE, "contrast", frames, os.path.join(m, "under_%s.png" % screen),
                        os.path.join(m, "boxes_%s.txt" % screen), outp], check=True, stdout=subprocess.DEVNULL)
        res[screen] = open(outp).read()
    return res


def previews(key, out, work, m):
    t = THEMES[key]
    prev = os.path.join(DRAFTS, "previews")
    os.makedirs(prev, exist_ok=True)
    fps = t["fps"]
    sh = "cd " + q(work) + "\n"
    for screen in ("main", "list"):
        base = os.path.join(prev, "%s-%s" % (t["folder"], screen))
        ov = q(os.path.join(m, screen + ".png"))
        inp = "-framerate {fps} -i frames/f%03d.png -i {ov} -filter_complex".format(fps=fps, ov=ov)
        # APNG: lossless, loops forever
        sh += "ffmpeg -v error -y {inp} '[0][1]overlay=format=auto' -plays 0 -f apng {o}\n".format(inp=inp, o=q(base + ".png"))
        # GIF: palette from the whole loop, light ordered dither (stable from frame to frame)
        sh += ("ffmpeg -v error -y {inp} '[0][1]overlay=format=auto,split[a][b];[a]palettegen=stats_mode=full:max_colors=256[p];"
               "[b][p]paletteuse=dither=bayer:bayer_scale=3' -loop 0 {o}\n").format(inp=inp, o=q(base + ".gif"))
        # 2x GIF for a closer look (pixels kept sharp)
        sh += ("ffmpeg -v error -y {inp} '[0][1]overlay=format=auto,scale=480:480:flags=neighbor,split[a][b];"
               "[a]palettegen=stats_mode=full:max_colors=256[p];[b][p]paletteuse=dither=bayer:bayer_scale=3' -loop 0 {o}\n").format(
            inp=inp, o=q(base + "-2x.gif"))
    # the bare background as a check of the device file itself: decode the .rgb565
    sh += ("ffmpeg -v error -y -f rawvideo -pix_fmt rgb565le -s 240x240 -r {fps} -i {a} -plays 0 -f apng {o}\n").format(
        fps=fps, a=q(os.path.join(out, "animations", t["anim"])), o=q(os.path.join(prev, t["folder"] + "-background.png")))
    sh += "magick {a} {b} +append {o}\n".format(a=q(os.path.join(m, "main.png")), b=q(os.path.join(m, "list.png")),
                                                o=q(os.path.join(m, "overlays.png")))
    wsl(sh)


def parse_contrast(text):
    rows = {}
    for line in text.splitlines()[1:]:
        m = re.match(r"(.+?)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)$", line.strip())
        if m:
            rows[m.group(1)] = tuple(float(m.group(i)) for i in (2, 3, 4))
    return rows


def make_index():
    """previews/index.html: the three drafts side by side, with their numbers."""
    report = json.load(open(os.path.join(BUILD, "report.json")))
    cards = []
    for key, t in THEMES.items():
        r = report.get(key)
        if not r:
            continue
        loop = FPS_FRAMES / t["fps"]
        rows = []
        for screen, title in (("main", "Main menu"), ("list", "Game list")):
            for label, (worst, p5, med) in parse_contrast(r[screen]).items():
                ok = "pass" if worst >= (3.0 if "battery" in label else 4.5) else "low"
                rows.append("<tr><td>%s: %s</td><td class='%s'>%.1f</td><td>%.1f</td></tr>" % (title, label, ok, worst, med))
        diff = re.search(r"(\d+) of", r["loop"]).group(1)
        st = re.search(r"at most (\d+)", r["loop"])
        step = (", by at most %s color step (float rounding)" % st.group(1)) if st and diff != "0" else ""
        cards.append("""
<article>
  <h2>{title} <span>{folder}</span></h2>
  <p>{summary}</p>
  <div class="shots">
    <figure><img src="{folder}-main.png" width="480" height="480" alt="{title}: main menu over the animated pond"><figcaption>Main menu</figcaption></figure>
    <figure><img src="{folder}-list.png" width="480" height="480" alt="{title}: game list over the animated pond"><figcaption>Game list</figcaption></figure>
  </div>
  <dl>
    <dt>Animation</dt><dd>{frames} frames, 240x240 RGB565, {fps} fps, {loop:.1f} s seamless loop</dd>
    <dt>Memory</dt><dd>{mb:.2f} MB frame file (mapped while the menu shows, released during games)</dd>
    <dt>Theme folder</dt><dd>{fmb:.1f} MB on the USB drive</dd>
    <dt>Loop check</dt><dd>{diff} of 57,600 pixels differ between the frame after the last and the first{step}</dd>
    <dt>Look</dt><dd>{palette}; font {font}</dd>
  </dl>
  <table><thead><tr><th>Text contrast (WCAG)</th><th>worst</th><th>median</th></tr></thead><tbody>{rows}</tbody></table>
</article>""".format(title=t["title"], folder=t["folder"], summary=t["summary"], frames=FPS_FRAMES, fps=t["fps"], loop=loop,
                     mb=r["bytes"] / 1e6, fmb=r["folder_bytes"] / 1e6, diff=diff, step=step, palette=t["palette"],
                     font=t["font"].replace("-TTF", "").replace(".ttf", ""), rows="".join(rows)))
    html = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Koi Theme Drafts</title>
<style>
:root { --bg: #0e1416; --card: #172124; --text: #e6eeec; --dim: #9fb3b0; --line: #2a3a3d; --ok: #8fd19e; --low: #f0a36b; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.45 system-ui, sans-serif; }
main { max-width: 1560px; margin: 0 auto; padding: 24px 16px 48px; }
h1 { font-size: 22px; margin: 0 0 6px; } .lead { color: var(--dim); margin: 0 0 22px; max-width: 70ch; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 500px), 1fr)); gap: 18px; }
article { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 16px; }
h2 { font-size: 18px; margin: 0 0 6px; } h2 span { color: var(--dim); font-weight: 400; font-size: 13px; margin-left: 6px; }
article > p { color: var(--dim); margin: 0 0 12px; }
.shots { display: grid; grid-template-columns: 1fr; gap: 10px; }
figure { margin: 0; } figure img { width: 100%; max-width: 480px; height: auto; image-rendering: pixelated; border-radius: 6px; display: block; }
figcaption { color: var(--dim); font-size: 13px; margin-top: 4px; }
dl { display: grid; grid-template-columns: max-content 1fr; gap: 4px 12px; margin: 14px 0; font-size: 14px; }
dt { color: var(--dim); } dd { margin: 0; }
table { border-collapse: collapse; width: 100%; font-size: 13px; }
th, td { text-align: left; padding: 3px 6px; border-bottom: 1px solid var(--line); } th { color: var(--dim); font-weight: 500; }
td.pass { color: var(--ok); } td.low { color: var(--low); }
</style></head><body><main>
<h1>Koi theme drafts for Mollusk OS</h1>
<p class="lead">Three directions for a koi pond launcher theme, each an animated 240x240 background with the menus drawn over it,
shown here at 2x. The previews are the device's exact frames (RGB565, dithered) with mockups of the main menu and the game list
positioned as in each layout.xml. Contrast is measured over every frame, under each text.</p>
<div class="grid">""" + "".join(cards) + """
</div></main></body></html>
"""
    with open(os.path.join(DRAFTS, "previews", "index.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write(html)
    # a still of all three side by side (main menu above, game list below), at 2x
    cols = []
    sh = ""
    for key, t in THEMES.items():
        work = os.path.join(BUILD, key)
        if not os.path.isfile(os.path.join(work, "mock", "main.png")):
            continue
        f = q(os.path.join(work, "frames", "f010.png"))
        col = q(os.path.join(BUILD, "col_%s.png" % key))
        sh += ("magick \\( {f} {m} -composite \\) \\( {f} {l} -composite \\) -append -scale 200% -background '#0e1416' "
               "-fill '#e6eeec' -font DejaVu-Sans-Bold -pointsize 22 -gravity north -splice 0x40 -annotate +0+8 '{title}' "
               "-gravity center -extent 500x1000 {o}\n").format(f=f, m=q(os.path.join(work, "mock", "main.png")),
                                                               l=q(os.path.join(work, "mock", "list.png")), title=t["title"], o=col)
        cols.append(col)
    if cols:
        sh += "magick %s +append -bordercolor '#0e1416' -border 10 %s\n" % (" ".join(cols), q(os.path.join(DRAFTS, "previews", "compare.png")))
        wsl(sh)


def main():
    if "--index-only" in sys.argv:
        make_index()
        return
    keys = [k for k in sys.argv[1:] if k in THEMES] or list(THEMES)
    copy_devices()
    report = {}
    for key in keys:
        out, work = build_theme(key)
        print("[%s] mockups" % key)
        m = mockups(key, out, work)
        print("[%s] contrast" % key)
        report[key] = contrast(key, work, m)
        print(report[key]["main"] + report[key]["list"])
        print("[%s] previews" % key)
        previews(key, out, work, m)
        loop = open(os.path.join(work, "loopcheck.txt")).read().strip()
        report[key]["loop"] = loop
        size = os.path.getsize(os.path.join(out, "animations", THEMES[key]["anim"]))
        report[key]["bytes"] = size
        total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(out) for f in fs)
        report[key]["folder_bytes"] = total
    rp = os.path.join(BUILD, "report.json")
    old = json.load(open(rp)) if os.path.isfile(rp) else {}
    old.update(report)
    json.dump(old, open(rp, "w"), indent=1)
    make_index()


if __name__ == "__main__":
    main()
