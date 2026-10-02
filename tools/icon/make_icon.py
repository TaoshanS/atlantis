#!/usr/bin/env python3
"""App icons composed from the user's own extracted assets (never committed: the art is Nickelodeon's).

  make_icon.py <extracted-dir> <out-dir>

Writes:
  icon_macos_1024.png  macOS template: 824x824 rounded body (continuous corners) with its drop shadow on a transparent 1024 canvas
  icon_ios_1024.png    iOS: full-bleed opaque square (the system applies the mask), plus the AppIcon*.png home-screen sizes
  AppIcon.iconset/     the macOS sizes, for `iconutil -c icns`
  icon.ico, icon_512.png  Windows / Linux
Content: the title screen's underwater backdrop and SpongeBob running with his card (the PLAY button art).
"""
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

BACKDROP = ("titlescreen", 10)   # 640x480 underwater title background
HERO = ("instructions", 94)      # SpongeBob running with a card (151x203)


def superellipse_mask(size, body, n=5.0, ss=4):
    """Apple-like continuous-corner rounded square of side `body` centred in `size` (antialiased)."""
    big = size * ss
    m = Image.new("L", (big, big), 0)
    px = m.load()
    half = body * ss / 2.0
    c = big / 2.0
    for y in range(big):
        dy = abs((y + 0.5 - c) / half)
        if dy > 1:
            continue
        dx = (1 - dy ** n) ** (1 / n) * half
        x0, x1 = int(round(c - dx)), int(round(c + dx))
        for x in range(max(0, x0), min(big, x1)):
            px[x, y] = 255
    return m.resize((size, size), Image.LANCZOS)


def artwork(ex, side):
    bg = Image.open(ex / "images" / BACKDROP[0] / ("%d.png" % BACKDROP[1])).convert("RGBA")
    # square crop around the upper middle of the backdrop (light rays), scaled to cover
    s = bg.height
    bg = bg.crop(((bg.width - s) // 2, 0, (bg.width + s) // 2, s)).resize((side, side), Image.LANCZOS)
    bg = bg.filter(ImageFilter.GaussianBlur(side / 160))  # soften the upscale; it is only a backdrop
    hero = Image.open(ex / "images" / HERO[0] / ("%d.png" % HERO[1])).convert("RGBA")
    k = side * 0.86 / hero.height
    hero = hero.resize((round(hero.width * k), round(hero.height * k)), Image.LANCZOS)
    shadow = Image.new("RGBA", hero.size, (0, 0, 0, 0))
    shadow.putalpha(hero.getchannel("A").point(lambda a: a * 0.45))
    shadow = shadow.filter(ImageFilter.GaussianBlur(side / 90))
    out = bg.copy()
    x = (side - hero.width) // 2 + round(side * 0.02)
    y = side - hero.height - round(side * 0.04)
    out.alpha_composite(shadow, (x + round(side * 0.015), y + round(side * 0.02)))
    out.alpha_composite(hero, (x, y))
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ex, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    # iOS: opaque full square
    ios = artwork(ex, 1024).convert("RGB")
    ios.save(out / "icon_ios_1024.png")
    for name, px in (("AppIcon60x60@2x", 120), ("AppIcon60x60@3x", 180), ("AppIcon76x76@2x", 152), ("AppIcon83.5x83.5@2x", 167)):
        ios.resize((px, px), Image.LANCZOS).save(out / (name + ".png"))  # listed in platforms/ios/Info.plist.in
    # macOS: body 824 on 1024, drop shadow (template: ~28 px blur, 12 px down, 50 % black)
    size, body = 1024, 824
    art = artwork(ex, body)
    mask = superellipse_mask(size, body)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sh = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sh.putalpha(ImageChops.offset(mask, 0, 12).point(lambda a: a * 0.5))
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(14)))
    body_img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    o = (size - body) // 2
    body_img.paste(art, (o, o))
    body_img.putalpha(ImageChops.multiply(body_img.getchannel("A"), mask))
    canvas.alpha_composite(body_img)
    # thin light inner edge, as on the system icons
    edge = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    inner = mask.filter(ImageFilter.MinFilter(5))
    edge.putalpha(ImageChops.subtract(mask, inner).point(lambda a: a * 0.25))
    canvas.alpha_composite(edge)
    canvas.save(out / "icon_macos_1024.png")
    iconset = out / "AppIcon.iconset"
    iconset.mkdir(exist_ok=True)
    for s in (16, 32, 128, 256, 512):
        canvas.resize((s, s), Image.LANCZOS).save(iconset / ("icon_%dx%d.png" % (s, s)))
        canvas.resize((s * 2, s * 2), Image.LANCZOS).save(iconset / ("icon_%dx%d@2x.png" % (s, s)))
    canvas.save(out / "icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])  # Windows
    canvas.resize((512, 512), Image.LANCZOS).save(out / "icon_512.png")  # Linux (.desktop)
    print("icons written to", out)


if __name__ == "__main__":
    main()
