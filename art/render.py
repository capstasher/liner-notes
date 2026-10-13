"""Render SVG/HTML to PNG with the bundled Chromium (no network needed)."""
import re
import sys
from playwright.sync_api import sync_playwright


def render_html(html, out_png, width, height, transparent=False, scale=1):
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": width, "height": height}, device_scale_factor=scale)
        pg.set_content(html, wait_until="load")
        pg.wait_for_timeout(150)
        pg.screenshot(path=out_png, omit_background=transparent, full_page=False)
        b.close()


def inline(path, w, h):
    svg = open(path, encoding="utf-8").read()
    svg = re.sub(r'width="\d+" height="\d+"', f'width="{w}" height="{h}"', svg, count=1)
    return svg


def sheet(svgs, out_png, cell=220, bg="#f0f0f0"):
    cells = "".join(
        f'<div style="display:inline-block;margin:6px;background:{bg};border-radius:8px;line-height:0">'
        f'{inline(s, cell, cell)}</div>' for s in svgs)
    html = f'<html><body style="margin:0;background:#fff">{cells}</body></html>'
    render_html(html, out_png, width=(cell + 12) * len(svgs), height=cell + 12)


if __name__ == "__main__":
    out = sys.argv[1]
    sheet(sys.argv[2:], out)
