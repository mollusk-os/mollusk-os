# Koi themes generator

These tools made the three koi launcher themes in `themes/` (KoiGlass,
KoiLilyPond, KoiMoonlight): their looping animated backgrounds, menu icons,
bars, layouts and splash screens, plus previews and text-contrast reports.
They run on Windows.

- `koi_frames.cpp` renders the loops. The koi are drawn by the koi renderer
  from my other projects (`koi_pond.h`, included unchanged),
  but each koi follows a fixed swim lane instead of steering, so the loop
  repeats exactly. Tail beats, fins, caustics, ripples, lanterns and fireflies
  all turn a whole number of times per loop. It also makes the icons and
  measures text contrast.
- `build.cmd` builds `koi_frames.exe` with Windhawk's clang. It looks for
  `koi_pond.h` in the folder that `KOI_RENDERER_DIR` names.
- `make_themes.py` builds the theme folders, mockups, previews and contrast
  reports. It reads the stock FunKey layout, art, fonts and sounds from the
  mollusk-os checkout in WSL (`MOLLUSK_OS_ROOT`) and uses ImageMagick and ffmpeg
  there.

The scripts expect to sit in a `tools/` folder inside a working folder (they
were run from `themes/koi-drafts/tools` next to the mollusk-os checkout): the
themes, `build/` and `previews/` are written next to that `tools/` folder.
Copy the theme folders you want into this repository's `themes/` afterwards.

```bat
tools\build.cmd
python tools\make_themes.py
```

Each frame file is 36 frames of 240x240 RGB565 (4 MB); see
`docs/animated-backgrounds.md` for the format.

## Licenses

The koi renderer comes from my other projects (MIT). Colors and the lily pad,
water lily, lantern and moon designs follow my other projects (CC BY-NC-SA
4.0). Both are by HourlySword. Fonts: Cabin (SIL OFL 1.1), Open
Sans and Roboto (Apache 2.0).
