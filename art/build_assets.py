"""Regenerate every image LinerNotes ships, from art/noto.py (which uses art/brush.py).

    python3 art/build_assets.py

Writes:
  art/noto-*.svg          the four moods, as vector sources
  res/noto-*.bmp          168 px sprites embedded in the DLL (32-bit, straight alpha)
  res/linernotes.ico      DLL / window icon
  docs/noto-*.png         README images

Needs Python 3, Pillow and Playwright's Chromium (pip install pillow playwright).
"""
import io
import os
import re
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import noto  # noqa: E402
from render import render_html  # noqa: E402

SPRITE = 168


def svg_sized(svg, w, h, viewbox=None):
    svg = re.sub(r'width="\d+" height="\d+"', f'width="{w}" height="{h}"', svg, count=1)
    if viewbox:
        svg = svg.replace('viewBox="0 0 400 400"', f'viewBox="{viewbox}"', 1)
    return svg


def rasterise(svg, w, h, path):
    html = f'<html><body style="margin:0;background:transparent;line-height:0">{svg}</body></html>'
    render_html(html, path, w, h, transparent=True)
    return Image.open(path).convert("RGBA")


def write_bmp32(img, path):
    """Bottom-up BI_RGB 32 bpp, straight alpha in the fourth byte."""
    w, h = img.size
    px = img.load()
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b, a = px[x, y]
            row += bytes((b, g, r, a))
        rows.append(bytes(row))
    pixels = b"".join(rows)
    header = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 32, 0, len(pixels), 2835, 2835, 0, 0)
    file_header = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(pixels), 0, 0, 14 + 40)
    with open(path, "wb") as f:
        f.write(file_header + header + pixels)


def main():
    os.makedirs(os.path.join(ROOT, "res"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    tmp = os.path.join(ROOT, "art", ".tmp.png")

    for mood in noto.MOODS:
        svg = noto.draw(mood)
        with open(os.path.join(HERE, f"noto-{mood}.svg"), "w", encoding="utf-8") as f:
            f.write(svg)
        img = rasterise(svg_sized(svg, SPRITE, SPRITE), SPRITE, SPRITE, tmp)
        write_bmp32(img, os.path.join(ROOT, "res", f"noto-{mood}.bmp"))
        big = rasterise(svg_sized(svg, 400, 400), 400, 400, tmp)
        big.save(os.path.join(ROOT, "docs", f"noto-{mood}.png"), optimize=True)
        print("mood", mood)

    # Icon: her face, cropped tight so it still reads at 16 px.
    svg = noto.draw("idle")
    face = rasterise(svg_sized(svg, 256, 256, viewbox="70 26 260 260"), 256, 256, tmp)
    face.save(os.path.join(ROOT, "res", "linernotes.ico"),
              sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)])
    face.save(os.path.join(ROOT, "docs", "noto-icon.png"), optimize=True)
    print("icon")
    os.remove(tmp)


if __name__ == "__main__":
    main()
