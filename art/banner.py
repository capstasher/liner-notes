"""README banner: Nōto beside the title, on a night-blue field with a faint disc.

    python3 art/banner.py [path/to/@fontsource/m-plus-rounded-1c]

Writes docs/banner.png (2560 x 840, shown at 1280 x 420). The title face is
M PLUS Rounded 1c (SIL OFL), from the npm package @fontsource/m-plus-rounded-1c;
without it the banner falls back to whatever rounded sans the system has.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import noto  # noqa: E402
from playwright.sync_api import sync_playwright  # noqa: E402

W, H = 1280, 420


def font_css(pkg):
    if not pkg or not os.path.isdir(pkg):
        return ""
    return "".join(f'<link rel="stylesheet" href="file://{pkg}/{w}.css">' for w in (400, 700, 800))


def disc(cx, cy, r):
    rings = "".join(
        f'<circle cx="{cx}" cy="{cy}" r="{r * k:.1f}" fill="none" stroke="#FFFFFF" stroke-opacity="{o}" stroke-width="1.2"/>'
        for k, o in ((1.0, .10), (.93, .05), (.86, .05), (.79, .05), (.72, .05), (.65, .05), (.58, .05), (.51, .05), (.44, .05)))
    return f"""
    <defs>
      <linearGradient id="sheen" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0.20" stop-color="#7FD4FF" stop-opacity="0"/>
        <stop offset="0.40" stop-color="#7FD4FF" stop-opacity="0.28"/>
        <stop offset="0.52" stop-color="#C69BFF" stop-opacity="0.28"/>
        <stop offset="0.64" stop-color="#FFB3D1" stop-opacity="0.24"/>
        <stop offset="0.80" stop-color="#FFE38A" stop-opacity="0"/>
      </linearGradient>
    </defs>
    <circle cx="{cx}" cy="{cy}" r="{r}" fill="#FFFFFF" fill-opacity="0.035"/>
    <circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#sheen)"/>
    {rings}
    <circle cx="{cx}" cy="{cy}" r="{r * .17:.1f}" fill="#1B1F4B" stroke="#FFFFFF" stroke-opacity=".18"/>
    <circle cx="{cx}" cy="{cy}" r="{r * .06:.1f}" fill="#12153A"/>"""


def sparkles():
    pts = [(560, 70, 9), (612, 330, 6), (1220, 60, 7), (760, 46, 5), (1188, 362, 6), (700, 372, 4)]
    star = lambda x, y, s: (f'<path d="M {x} {y - s} L {x + s * .28} {y - s * .28} L {x + s} {y} L {x + s * .28} {y + s * .28} '
                            f'L {x} {y + s} L {x - s * .28} {y + s * .28} L {x - s} {y} L {x - s * .28} {y - s * .28} Z" '
                            f'fill="#FFC062" fill-opacity=".85"/>')
    notes = [(640, 120, 30, -12), (1236, 196, 24, 10), (690, 300, 20, 8)]
    note = lambda x, y, s, rot: (f'<text x="{x}" y="{y}" font-size="{s}" fill="#FFFFFF" fill-opacity=".22" '
                                 f'transform="rotate({rot} {x} {y})" font-family="DejaVu Sans">&#9834;</text>')
    return "".join(star(*p) for p in pts) + "".join(note(*n) for n in notes)


def html(pkg):
    who = noto.draw("happy", size=440)
    return f"""<!doctype html><html><head><meta charset="utf-8">{font_css(pkg)}
<style>
  html, body {{ margin: 0; }}
  body {{ width: {W}px; height: {H}px; overflow: hidden; position: relative;
         background: radial-gradient(120% 140% at 85% 30%, #3B4296 0%, #262A66 45%, #15183F 100%);
         font-family: "M PLUS Rounded 1c", "Nunito", "Varela Round", sans-serif; }}
  .fx {{ position: absolute; inset: 0; }}
  .who {{ position: absolute; right: 70px; bottom: -14px; width: 440px; height: 440px;
         /* rim light: keeps her dark hair from melting into the night-blue field */
         filter: drop-shadow(0 0 1.5px rgba(214, 220, 255, .95)) drop-shadow(0 0 10px rgba(150, 160, 255, .55))
                 drop-shadow(0 12px 26px rgba(5, 6, 30, .6)); }}
  .text {{ position: absolute; left: 76px; top: 70px; color: #FFF6EA; }}
  .kana {{ font-weight: 700; font-size: 22px; letter-spacing: .32em; color: #FFC062; }}
  h1 {{ margin: 6px 0 0; font-weight: 800; font-size: 104px; line-height: 1; letter-spacing: -.01em;
        text-shadow: 0 4px 0 rgba(10, 12, 40, .35); }}
  h1 span {{ color: #FF8A6B; }}
  .sub {{ margin-top: 14px; font-weight: 700; font-size: 25px; color: #C9CDF5; }}
  .bubble {{ display: inline-block; margin-top: 30px; padding: 12px 22px; border-radius: 22px;
            background: #FFF6EA; color: #262A66; font-weight: 700; font-size: 23px; position: relative;
            box-shadow: 0 6px 18px rgba(5, 6, 30, .35); }}
  .bubble::after {{ content: ""; position: absolute; right: -12px; top: 50%; margin-top: -9px;
                   border: 9px solid transparent; border-left: 14px solid #FFF6EA; border-right: 0; }}
  .bubble b {{ color: #D9661E; }}
</style></head><body>
  <svg class="fx" viewBox="0 0 {W} {H}" width="{W}" height="{H}">{disc(1010, 210, 250)}{sparkles()}</svg>
  <div class="who">{who}</div>
  <div class="text">
    <div class="kana">ライナーノーツ</div>
    <h1>Liner<span>Notes</span></h1>
    <div class="sub">CD album info for Windows Media Player 12</div>
    <div class="bubble">Your CDs have names again <b>&#9834;</b></div>
  </div>
</body></html>"""


def main():
    pkg = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("MPLUS_ROUNDED_DIR", "")
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    page_path = os.path.join(HERE, ".banner.html")
    with open(page_path, "w", encoding="utf-8") as f:
        f.write(html(pkg))
    out = os.path.join(ROOT, "docs", "banner.png")
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": W, "height": H}, device_scale_factor=2)
        pg.goto("file://" + page_path, wait_until="networkidle")
        pg.evaluate("document.fonts.ready")
        pg.wait_for_timeout(300)
        pg.screenshot(path=out)
        b.close()
    os.remove(page_path)
    print("wrote", out)


if __name__ == "__main__":
    main()
