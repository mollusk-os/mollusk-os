# Add-on packs

Games and extra themes don't come with Mollusk OS firmware updates. They come
in add-on packs, which you install once and keep across firmware updates:

| Pack | What's in it |
|---|---|
| `MolluskOS-Games-1.0.zip` | The freeware and homebrew games that came with FunKey OS (Atari Lynx, Game Boy, Game Boy Color, Game Boy Advance, Game Gear, Neo Geo Pocket, NES, PC Engine, PICO-8, Pokémon Mini, SNES, Mega Drive, Master System), the playable demos of Wolfenstein 3D, Spear of Destiny, Doom, Quake and Quake II, and EDuke32 with the Duke Nukem 3D shareware episode |
| `MolluskOS-Themes-1.0.zip` | Animated launcher themes: AnimatedDemo, KoiGlass, KoiLilyPond and KoiMoonlight |

- **A new SD card** made from the Mollusk OS image comes with the games: the
  first boot installs them, as FunKey OS always did.
- **A firmware update** (`.fwu`) no longer copies games onto the USB drive,
  so games you deleted stay deleted, and the update file is about 100 MB
  smaller. The launcher theme you picked is kept too (when updating from a
  Mollusk OS release with packs).
- **KoiGlass** is built into the firmware and is the default theme. The
  themes pack has it too, so that the pack holds the whole koi set.

## Installing a pack

1. In the FunKey menu, choose **MOUNT USB** and connect the FunKey to your
   computer.
2. Copy the pack's zip file, as it is (don't unzip it), to the root of the
   FunKey's drive.
3. Eject the drive on your computer, then choose **EJECT USB** on the
   FunKey.

The FunKey installs the pack ("INSTALLING THE GAMES PACK..."), deletes the
zip file and restarts the launcher, which then shows the new games and
themes. The games pack takes a minute or two: don't turn the console off
while it installs. A pack copied to the drive while the FunKey is off gets
installed at the next boot.

Installing a pack again (or a newer version) overwrites its files with fresh
copies and leaves everything else alone.

### Reinstalling the games

If the system came from the SD card image, **Freewares** (in the Settings
section of the GMenu2X launcher) reinstalls the games from the copy kept in
the firmware. After a firmware update that copy is gone: copy the games pack
to the drive instead.

## Making a pack

A pack is a zip file named `MolluskOS-<Name>-<version>.zip`. Its contents
mirror the FunKey's USB drive, plus a `pack.ini` at its root:

```ini
# Mollusk OS add-on pack
name=Themes
version=1.0
description=Animated launcher themes
```

`name` is required: a zip without it is not a pack, and the FunKey leaves it
alone. For example, a pack with one theme and one game:

```
MolluskOS-MyStuff-1.0.zip
├── pack.ini
├── FunKey/.retrofe/layouts/MyTheme/layout.xml
├── FunKey/.retrofe/layouts/MyTheme/...
└── SNES/My Homebrew.sfc
```

When a pack is installed:

- every file is extracted onto the drive, overwriting older copies;
- every theme in it (a `FunKey/.retrofe/layouts/<theme>/layout.xml`) is added
  to the user theme list, `FunKey/.retrofe/layouts/layouts.list`, unless the
  list has it already or a built-in theme has the same name;
- its `pack.ini` is kept as `FunKey/.packs/<name>.ini`;
- if the drive doesn't have room for all its files, nothing is installed.

The drive is FAT32: no symbolic links, and file names are not case sensitive.

`tools/make-pack.py` builds packs (`make packs` builds the two above):

```bash
python3 tools/make-pack.py --name MyStuff --version 1.0 --output MolluskOS-MyStuff-1.0.zip \
    my-themes/MyTheme=FunKey/.retrofe/layouts/MyTheme  my-roms=SNES
```

Each source is a folder or a zip file, followed by `=` and the folder of the
drive it goes into (the root when left out). Symbolic links are followed, and
the zip is reproducible.

On the FunKey, packs are installed by `/usr/local/sbin/pack`
(`pack install FILE`, or `pack scan` for the drive's root), which the
`share` script runs whenever it mounts the drive, and `first_boot` runs for
the packs that come with the SD card image.

## In this repository

- `packs/games/`: the games pack's files (`freeware_games.zip` is the
  freeware archive FunKey OS used to carry, unchanged). EDuke32's OPK is built
  by the `eduke32` package into `FunKey/output/images/packs/games/`.
- `themes/`: the launcher themes. `FunKey/board/funkey/post-build.sh` copies
  the built-in ones (KoiGlass) into the firmware; the themes pack has the
  ones listed in `THEMES_PACK_THEMES` in the `Makefile`.
- The SD card image's root filesystem is the firmware's plus the games pack,
  in `/usr/local/share/packs` (`tools/make-sdcard-rootfs.sh`). The firmware
  update file uses the plain one.

## Licenses

The games keep their own licenses: each system folder has a
`Copyrights.txt`. The demos of the FPS classics are the freely distributable
shareware or demo releases of their games. The koi themes' art is by
HourlySword (koi from Glass Wings, MIT; pond designs after Koi Pond Animated,
CC BY-NC-SA 4.0); see each theme's `Credits.txt`.
