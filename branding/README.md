# Mollusk OS branding

## Logo

The Mollusk OS logo is a cuttlefish made of dots. A cuttlefish's skin changes color and
pattern at will; here it is made of dots, like screen pixels, with bands of color passing
over them, the way Mollusk OS changes themes. "MOLLUSK OS" is in dot-matrix lettering in the
same colors, drawn as dots, so no font is involved.

| File | What it is |
|---|---|
| `logo/symbol.svg` | The cuttlefish alone, transparent background, in the colors for light backgrounds |
| `logo/symbol-dark.svg` | The same in lighter colors, for dark backgrounds |
| `logo/logo-light.svg` | Cuttlefish and name side by side, for light backgrounds |
| `logo/logo-dark.svg` | The same, for dark backgrounds |
| `logo/logo-stacked-dark.svg` | The cuttlefish above the name, for dark backgrounds (the boot screen's layout) |

## Boot logos

The kernel shows a 240x240 logo while the FunKey boots. It is built from a plain PPM with at
most 224 colors:

| File | When it shows |
|---|---|
| `boot/logo_funkey_clut224.ppm` | Normal boot: the stacked logo, centred |
| `boot/logo_funkey_recovery_clut224.ppm` | Recovery: a smaller logo above the wrench and screwdriver of the FunKey OS Recovery logo (`boot/recovery-tools.png`) |

`FunKey/external.mk` and `Recovery/external.mk` copy them into the kernel sources over the
FunKey ones (the file names stay the kernel's). A firmware update (`.fwu`) brings the new
normal boot logo; the Recovery partition, and so its logo, only changes with a fresh SD card
image.

`boot/make-boot-logos.py` makes both from `logo/logo-stacked-dark.svg` (it needs
`rsvg-convert` and Pillow); `*-2x.png` are previews. After changing them, rebuild the kernels
from scratch so the logos are copied again:

```bash
docker/build.sh FunKey/linux-dirclean Recovery/linux-dirclean all
```

The logo was chosen from several drafts (kept outside the repository, with the tool that
draws them). It replaced the first logo, a nautilus, before the first release.
