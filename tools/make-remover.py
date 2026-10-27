#!/usr/bin/env python3
"""Build the remover of a Mollusk OS add-on pack.

A remover is a pack too: a MolluskOS-*.zip copied to the root of the USB drive, whose
pack.ini names the pack it removes (removes=Games). Instead of files to install it
carries remove.txt: the pack's files with their checksums, and the pack's own folders.
The FunKey's pack tool then deletes each listed file that still matches one of its
checksums: a file that changed since it was installed, like a game's config holding
your settings, is kept. Then it deletes the listed folders that are left empty. Saves
and settings the games created are never in the list, so they stay.

Usage: make-remover.py --pack PACK.zip --output REMOVER.zip [--also KNOWN.md5]...

--also adds other known versions of the pack's files, in md5sum format ("md5  path"),
for instance an OPK as an earlier firmware shipped it.
"""

import argparse
import hashlib
import os
import sys
import zipfile

# Folders the FunKey creates whenever it mounts the USB drive (mount_share in the share
# script), and FunKey/ with everyone's settings: never removed, even when empty
KEEP = {
    "PICO-8", "Pokemon Mini", "Final Burn Alpha 2012", "MAME 2000", "Libretro",
    "Libretro/cores", "Native games", "Applications", "Emulators", "Atari lynx",
    "Game Boy", "Game Boy Color", "Game Boy Advance", "Game Gear", "Neo Geo Pocket",
    "NES", "PS1", "Sega Genesis", "Sega Master System", "SNES", "WonderSwan",
    "PCE-TurboGrafx", "FunKey",
}

FIXED_DATE = (2026, 1, 1, 0, 0, 0)


def entry(path):
    info = zipfile.ZipInfo(path, FIXED_DATE)
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def main():
    parser = argparse.ArgumentParser(description="Build the remover of a Mollusk OS add-on pack")
    parser.add_argument("--pack", required=True, help="the pack to remove")
    parser.add_argument("--output", "-o", required=True)
    parser.add_argument("--also", action="append", default=[], metavar="KNOWN.md5",
                        help="other known versions of the pack's files (md5sum format)")
    args = parser.parse_args()

    with zipfile.ZipFile(args.pack) as z:
        meta = {}
        for line in z.read("pack.ini").decode("utf-8").splitlines():
            if "=" in line and not line.lstrip().startswith("#"):
                key, value = line.split("=", 1)
                meta[key.strip()] = value.strip()
        name, version = meta["name"], meta["version"]
        sums, dirs = {}, set()
        for info in z.infolist():
            path = info.filename.rstrip("/")
            if path == "pack.ini":
                continue
            if info.is_dir():
                dirs.add(path)
                continue
            sums[path] = [hashlib.md5(z.read(info)).hexdigest()]
            parts = path.split("/")[:-1]
            for i in range(1, len(parts) + 1):
                dirs.add("/".join(parts[:i]))

    for known in args.also:
        with open(known, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                md5, path = line.split(None, 1)
                path = path.lstrip("*")                # md5sum's binary-mode marker
                if path not in sums:
                    sys.exit(f"make-remover: {known}: {path} is not in {args.pack}")
                if md5 not in sums[path]:
                    sums[path].append(md5)

    removable = [d for d in dirs if d not in KEEP and d.split("/")[0] != "FunKey"]
    lines = [
        f"# Mollusk OS: what removing the {name} pack {version} deletes",
        "# file <md5>[,<md5>...] <path>: deleted if it still matches one of the checksums",
        "# dir <path>: deleted if it is empty (the deepest first)",
    ]
    lines += [f"file {','.join(sums[p])} {p}" for p in sorted(sums)]
    lines += [f"dir {d}" for d in sorted(removable, key=lambda d: (-d.count("/"), d))]

    manifest = (f"# Mollusk OS add-on pack\nname={name} remover\nversion={version}\n"
                f"description=Removes what the {name} pack installed, keeping saves and settings\n"
                f"removes={name}\n")
    tmp = args.output + ".tmp"
    with zipfile.ZipFile(tmp, "w") as z:
        z.writestr(entry("pack.ini"), manifest, zipfile.ZIP_DEFLATED)
        z.writestr(entry("remove.txt"), "\n".join(lines) + "\n", zipfile.ZIP_DEFLATED, 9)
    os.replace(tmp, args.output)
    print(f"{args.output}: removes the {name} pack {version}: {len(sums)} files "
          f"({sum(len(v) - 1 for v in sums.values())} with other known versions), "
          f"{len(removable)} folders when empty")


if __name__ == "__main__":
    main()
