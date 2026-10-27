# Animated backgrounds

Mollusk OS's RetroFE adds an `<animatedImage>` layout component: a looping
sequence of frames, meant for animated menu backgrounds. It is built for the
FunKey S (one 1.2 GHz Cortex-A7 core, 64 MB of RAM, no GPU):

- Frames are stored ready to blit (RGB565), so nothing is decoded while the
  menu runs.
- Each frame is read from the frame file when it is due, so the frames sit
  in the kernel's page cache rather than in RetroFE's heap (which only holds
  the frame on screen). The file is open only while a frame is read, so a
  theme on the USB drive doesn't keep the drive busy and **MOUNT USB** still
  works; while the drive is shared with a computer, the last frame stays on
  screen.
- The screen is only redrawn when the animation moves to its next frame, at
  the frame rate the theme asks for (capped by `animationMaxFps`).
- After `animationIdleTimeout` seconds without a button press, the animation
  slows down to the theme's `idleFps`. At the default `idleFps` of 0 it
  pauses, and nothing is redrawn until a button is pressed.
- When a game is launched, the file's cached frames are dropped, so the game
  gets the memory back. They are read again when the game exits.

Every new frame redraws the whole menu, which takes 9 to 11 ms on a FunKey S
with the stock themes' layouts. So the frame rate is what costs CPU: while
animating, AnimatedDemo (12 fps) uses about 17% of the CPU and KoiGlass
(9 fps) about 16%. Reading each frame (rather than mapping the file) adds
about 0.75 ms per redraw. Once the animation pauses for idling, the cost is
zero.

## Try it

The default theme, **KoiGlass**, has an animated koi pond background (36
frames, 240x240, 9 fps, about 4 MB). More animated themes come in the themes
pack (see [packs.md](packs.md)): **AnimatedDemo** (the Classic theme with
animated water caustics), **KoiLilyPond** and **KoiMoonlight**. Pick one with
**SET THEME** in the FunKey menu.

The theme sources are in the repository's `themes/` folder.

## Layout XML

```xml
<animatedImage src="animations/caustics.rgb565" frameWidth="240" frameHeight="240"
               fps="12" idleFps="0" x="0" y="0" layer="0"/>
```

| Attribute | Default | Meaning |
|---|---|---|
| `src` | (required) | Frame file, relative to the theme folder |
| `frameWidth`, `frameHeight` | screen size (240x240) | Size of one frame in the file |
| `fps` | 15 | Frames per second while the menu is in use |
| `idleFps` | 0 | Frames per second after the idle timeout (0 = pause) |
| `loop` | true | `false` plays once and holds the last frame |
| `transparentColor` | none | `RRGGBB` color that is not drawn (compared after conversion to RGB565) |

The usual position and look attributes work too (`x`, `y`, `xOrigin`,
`yOrigin`, `layer`, `alpha`, `width`, `height`, ...), as do animation events
such as `<onEnter>` and `<onExit>`. Within a layer, animated images are drawn
before every other component, so a background can share layer 0 with other
images.

Make the frames the size they are shown at. If `width`/`height` differ from
the frame size, every frame is rescaled on every redraw, which is slow; RetroFE
logs a warning when that happens.

## settings.conf

| Key | Default | Meaning |
|---|---|---|
| `animationEnable` | yes | `no` shows only the first frame of every animated image |
| `animationMaxFps` | 30 | Highest frame rate any animated image may use |
| `animationIdleTimeout` | 30 | Seconds without a button press before animations drop to `idleFps` (0 = never) |
| `renderStatsFile` | (unset) | For testing: a file that gets one line of redraw, CPU and memory statistics every 5 seconds |

`/usr/games/settings.conf` is on the read-only root filesystem: run `rw`
before editing it and `ro` afterwards.

## Frame file format

One file holding every frame back to back, with no header. Each frame is
`frameWidth x frameHeight` pixels of 16-bit RGB565, little endian, row by row.
A 240x240 frame is 115,200 bytes, so:

| Loop | 12 fps | 15 fps | 30 fps |
|---|---|---|---|
| 2 s | 2.8 MB | 3.5 MB | 6.9 MB |
| 3 s | 4.1 MB | 5.2 MB | 10.4 MB |

The whole file ends up in RAM (as page cache) while the menu is showing, so
keep it to a few megabytes.

ffmpeg writes this format directly. The final
`scale=sws_dither=bayer,format=rgb565le` step uses an ordered dither, which
avoids color banding without making still areas shimmer from frame to frame.

From a video clip, cropped to fill the screen:

```bash
ffmpeg -i clip.mp4 -t 3 -vf "fps=12,scale=240:240:force_original_aspect_ratio=increase,crop=240:240,scale=sws_dither=bayer,format=rgb565le" -f rawvideo -pix_fmt rgb565le background.rgb565
```

From numbered PNG frames:

```bash
ffmpeg -framerate 12 -i frame_%03d.png -vf "scale=240:240,scale=sws_dither=bayer,format=rgb565le" -f rawvideo -pix_fmt rgb565le background.rgb565
```

From a pixel-art GIF (no smoothing):

```bash
ffmpeg -i anim.gif -vf "scale=240:240:flags=neighbor,scale=sws_dither=bayer,format=rgb565le" -f rawvideo -pix_fmt rgb565le background.rgb565
```

To check a file on a PC, turn it back into a GIF:

```bash
ffmpeg -f rawvideo -pix_fmt rgb565le -s 240x240 -r 12 -i background.rgb565 preview.gif
```

For a seamless loop, the last frame has to lead into the first one; a clip cut
from a longer video usually jumps at the loop point. AnimatedDemo's frames
come from `tools/make-caustics-animation.py`, and the koi themes' from
`tools/koi-themes/`: both only use motion that repeats a whole number of
times per loop.

## Measuring on the FunKey S

1. Run `rw`, then add `renderStatsFile = /tmp/retrofe-stats.log` to
   `/usr/games/settings.conf`, then run `ro`. Restart RetroFE (for example
   with `pkill retrofe`; the frontend loop starts it again).
2. Watch the statistics with `tail -f /tmp/retrofe-stats.log`. Each line
   covers 5 seconds:

   ```
   t=1307s renders=60 (12.0/s) draw=9.36ms avg 9.83ms max flip=2.42ms avg cpu=17.0% rss=12052kB anon=4840kB file=7212kB anim_file=4050kB
   ```

   (AnimatedDemo on a FunKey S, while animating.)

   - `renders`: screen redraws. Should match the animation's fps while
     animating, and drop to 0 when idle on a still screen.
   - `draw`: time to draw the whole page; `flip`: time to copy it to the
     display.
   - `cpu`: RetroFE's CPU use over those 5 seconds (user plus system).
   - `rss`/`anon`/`file`: RetroFE's resident memory. The cached frames are
     not part of it: they are in the page cache (`Cached` in
     `/proc/meminfo`). `anim_file` is the size of the frame files in use.
3. RetroFE's log (`/tmp/retrofe.log`) has a line for each frame file it
   starts and stops using (`[AnimatedImage] Using 36 frames ...`,
   `[AnimatedImage] Released ...`), warns about wrongly sized files, and
   notes when a frame file can't be read (while the USB drive is shared).
