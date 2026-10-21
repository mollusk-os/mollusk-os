# Animated backgrounds

Mollusk OS's RetroFE adds an `<animatedImage>` layout component: a looping
sequence of frames, meant for animated menu backgrounds. It is built for the
FunKey S (one 1.2 GHz Cortex-A7 core, 64 MB of RAM, no GPU):

- Frames are stored ready to blit (RGB565), so nothing is decoded while the
  menu runs.
- The frame file is memory-mapped, so the frames sit in the kernel's page
  cache rather than in RetroFE's heap.
- The screen is only redrawn when the animation moves to its next frame, at
  the frame rate the theme asks for (capped by `animationMaxFps`).
- After `animationIdleTimeout` seconds without a button press, the animation
  slows down to the theme's `idleFps`. At the default `idleFps` of 0 it
  pauses, and nothing is redrawn until a button is pressed.
- When a game is launched, the file is unmapped and its cached frames are
  dropped, so the game gets the memory back. They are mapped again when the
  game exits.

## Try it

Pick the **AnimatedDemo** theme in the FunKey menu (the theme option). It is
the Classic theme with an animated water-caustics background (36 frames,
240x240, 12 fps, about 4 MB).

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
from a longer video usually jumps at the loop point. The demo's frames come
from `tools/make-caustics-animation.py`, which only uses motion that repeats
exactly once per loop.

## Measuring on the FunKey S

1. Run `rw`, then add `renderStatsFile = /tmp/retrofe-stats.log` to
   `/usr/games/settings.conf`, then run `ro`. Restart RetroFE (for example
   with `pkill retrofe`; the frontend loop starts it again).
2. Watch the statistics with `tail -f /tmp/retrofe-stats.log`. Each line
   covers 5 seconds:

   ```
   t=313s renders=60 (12.0/s) draw=0.66ms avg 1.14ms max flip=0.34ms avg cpu=3.7% rss=33384kB anon=7992kB file=25164kB anim_mapped=4050kB
   ```

   - `renders`: screen redraws. Should match the animation's fps while
     animating, and drop to 0 when idle on a still screen.
   - `draw`: time to draw the whole page; `flip`: time to copy it to the
     display.
   - `cpu`: RetroFE's CPU use over those 5 seconds (user plus system).
   - `rss`/`anon`/`file`: RetroFE's resident memory; `file` includes the
     cached frames. `anim_mapped` is the size of the mapped frame files.
3. RetroFE's log (`/tmp/retrofe.log`) has a line for each frame file it maps
   and releases (`[AnimatedImage] Mapped 36 frames ...`, `[AnimatedImage]
   Released ...`), and warns about wrongly sized files.
