#!/usr/bin/env python3
"""Generate the 256x256 JPEG icon embedded into the NRO's NACP.

Switch's hbmenu reads the icon straight out of the NACP icon region.  It must be
a baseline JPEG, 256x256, and should stay well under the 0x20000 byte region that
nacptool reserves.  Keeping the generator in-tree means the artwork is
reproducible instead of being an unexplained binary blob.

Usage:
    python3 nx/tools/mkicon.py [output.jpg]

Requires Pillow.  The source artwork is the desktop app's own logo so the Switch
build matches the other platforms.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 256

# Palette lifted from nx/source/ui/ui_shell.cpp ("暮色" theme): the launcher icon
# then reads as the same product as the in-app UI.
BG_TOP = (0x0A, 0x0D, 0x14)
BG_BOTTOM = (0x14, 0x1A, 0x26)
GLOW = (0x5B, 0x9D, 0xFF)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def find_logo():
    for rel in (
        "public/icons/logo-icon-1024x1024.png",
        "public/icons/logo-icon.png",
        "public/icons/favicon-512x512.png",
    ):
        path = os.path.join(REPO, rel)
        if os.path.exists(path):
            return path
    raise SystemExit("logo artwork not found under public/icons/")


def vertical_gradient(size, top, bottom):
    img = Image.new("RGB", (1, size[1]))
    px = img.load()
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        px[0, y] = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
    return img.resize(size, Image.BILINEAR)


def radial_glow(side, color, max_alpha=46):
    """Soft accent halo behind the logo, drawn small then upscaled (cheap blur)."""
    small = max(8, side // 4)
    img = Image.new("L", (small, small), 0)
    d = ImageDraw.Draw(img)
    r = small * 0.42
    c = small / 2.0
    d.ellipse([c - r, c - r, c + r, c + r], fill=255)
    img = img.filter(ImageFilter.GaussianBlur(small * 0.13)).resize((side, side), Image.BICUBIC)
    alpha = img.point(lambda v: v * max_alpha // 255)
    layer = Image.new("RGBA", (side, side), color + (0,))
    layer.putalpha(alpha)
    return layer


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "nx", "icon.jpg")

    canvas = vertical_gradient((SIZE, SIZE), BG_TOP, BG_BOTTOM).convert("RGBA")
    canvas = Image.alpha_composite(canvas, radial_glow(SIZE, GLOW))

    logo = Image.open(find_logo()).convert("RGBA")
    # No bbox crop: the artwork already carries its own optical padding, and the
    # glyph is taller than it is wide, so cropping would shrink it inside the plate.
    box = int(SIZE * 0.92)
    scale = min(box / logo.width, box / logo.height)
    logo = logo.resize(
        (max(1, round(logo.width * scale)), max(1, round(logo.height * scale))),
        Image.LANCZOS,
    )

    pos = ((SIZE - logo.width) // 2, (SIZE - logo.height) // 2)
    # Faint drop shadow keeps the coral note separated from the navy plate.
    shadow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", logo.size, (0, 0, 0, 110)), (pos[0], pos[1] + 4), logo)
    shadow = shadow.filter(ImageFilter.GaussianBlur(6))
    canvas = Image.alpha_composite(canvas, shadow)
    canvas.paste(logo, pos, logo)

    # NACP wants plain JPEG; flatten alpha and drop the ICC profile.
    final = canvas.convert("RGB")
    final.save(out, "JPEG", quality=92, optimize=True, progressive=False, subsampling=0)

    size = os.path.getsize(out)
    print(f"wrote {out} ({SIZE}x{SIZE}, {size} bytes)")
    return 0 if size < 0x20000 else 1


if __name__ == "__main__":
    sys.exit(main())
