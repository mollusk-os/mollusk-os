#!/usr/bin/env python3
"""Build a Mollusk OS add-on pack.

A pack is a zip file whose contents mirror the FunKey's USB drive, with a
pack.ini at its root. Copied to the root of the USB drive, it is installed
when the drive is ejected or at the next boot (see the "pack" script in the
firmware, and docs/packs.md).

Usage: make-pack.py --name NAME --version VERSION --output FILE
                    [--description TEXT] SOURCE[=DEST]...

Each SOURCE is a directory, whose contents go into the DEST folder of the
drive, or a zip file, whose entries go there. DEST defaults to the root of
the drive. Symbolic links are followed (the drive is FAT32). When two
sources have the same file, the later one wins.

The zip is reproducible: entries are sorted and get a fixed date.
"""

import argparse
import os
import sys
import zipfile

# Already compressed: stored as they are
STORED_EXTENSIONS = {".opk", ".zip", ".png", ".jpg", ".jpeg", ".gz", ".xz", ".7z", ".chd"}

FIXED_DATE = (2026, 1, 1, 0, 0, 0)


def clean_path(path, what):
    path = path.replace("\\", "/").strip("/")
    parts = [p for p in path.split("/") if p not in ("", ".")]
    if any(p == ".." for p in parts):
        sys.exit("make-pack: %s goes outside the drive: %s" % (what, path))
    return "/".join(parts)


def join(dest, rel):
    return "%s/%s" % (dest, rel) if dest else rel


def collect(sources):
    """Return {path in the pack: (kind, source)} for every file, and the folders the
    sources list, so that empty ones (e.g. a place for the user to put something) are
    kept."""
    files, folders = {}, set()
    for spec in sources:
        src, _, dest = spec.partition("=")
        dest = clean_path(dest, "destination")
        if os.path.isdir(src):
            for root, dirs, names in os.walk(src, followlinks=True):
                dirs.sort()
                if not dirs and not names and root != src:
                    folders.add(join(dest, clean_path(os.path.relpath(root, src), "folder")))
                for name in sorted(names):
                    full = os.path.join(root, name)
                    if not os.path.isfile(full):
                        sys.exit("make-pack: not a file (broken link?): %s" % full)
                    rel = clean_path(os.path.relpath(full, src), "file")
                    files[join(dest, rel)] = ("file", full)
        elif zipfile.is_zipfile(src):
            with zipfile.ZipFile(src) as z:
                for info in z.infolist():
                    rel = clean_path(info.filename, "zip entry")
                    if info.is_dir():
                        folders.add(join(dest, rel))
                    else:
                        files[join(dest, rel)] = ("zip", (src, info.filename))
        else:
            sys.exit("make-pack: not a directory or a zip file: %s" % src)
    if "pack.ini" in files:
        sys.exit("make-pack: pack.ini is written by this tool, don't include one")
    return files, folders


def read(kind, source):
    if kind == "file":
        with open(source, "rb") as f:
            return f.read()
    zip_path, name = source
    with zipfile.ZipFile(zip_path) as z:
        return z.read(name)


def entry(path, is_dir=False):
    info = zipfile.ZipInfo(path + ("/" if is_dir else ""), FIXED_DATE)
    info.create_system = 3  # Unix, so the permissions below are used
    if is_dir:
        info.external_attr = (0o40755 << 16) | 0x10
    else:
        info.external_attr = 0o100644 << 16
    return info


def main():
    parser = argparse.ArgumentParser(description="Build a Mollusk OS add-on pack")
    parser.add_argument("--name", required=True, help="pack name, e.g. Games")
    parser.add_argument("--version", required=True)
    parser.add_argument("--description", default="")
    parser.add_argument("--output", "-o", required=True)
    parser.add_argument("sources", nargs="+", metavar="SOURCE[=DEST]")
    args = parser.parse_args()

    files, folders = collect(args.sources)
    if not files:
        sys.exit("make-pack: nothing to pack")

    # every folder holding a file, and the folders that hold none (kept as entries)
    dirs = set()
    for path in list(files) + [f + "/" for f in folders]:
        parts = path.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            dirs.add("/".join(parts[:i]))

    manifest = "# Mollusk OS add-on pack\nname=%s\nversion=%s\n" % (args.name, args.version)
    if args.description:
        manifest += "description=%s\n" % args.description

    tmp = args.output + ".tmp"
    total = 0
    with zipfile.ZipFile(tmp, "w") as z:
        z.writestr(entry("pack.ini"), manifest, zipfile.ZIP_DEFLATED)
        for path in sorted(dirs | set(files)):
            if path in dirs:
                z.writestr(entry(path, True), b"")
                continue
            data = read(*files[path])
            total += len(data)
            ext = os.path.splitext(path)[1].lower()
            method = zipfile.ZIP_STORED if ext in STORED_EXTENSIONS else zipfile.ZIP_DEFLATED
            z.writestr(entry(path), data, method, 9)
    os.replace(tmp, args.output)

    print("%s: %s %s, %d files, %.1f MB on the drive, %.1f MB zipped"
          % (args.output, args.name, args.version, len(files),
             total / 1e6, os.path.getsize(args.output) / 1e6))


if __name__ == "__main__":
    main()
