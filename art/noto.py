"""Nōto, the LinerNotes mascot, in outlined cel-shaded style.

An original character, drawn from primitives. Each part is an SVG layer
(inkscape:label), so the file opens with named layers in Inkscape or Krita:

  hair-back, body, accessories, face, eyes, mouth, hair-front, brows, hair-clip,
  arms, booklet, hands

Light comes from the upper left: shadows sit to the right and under overhangs.
Lines are tapered with brush.brush(); big silhouettes use a steady contour.
"""
import math
import sys

from brush import brush, sample

INK = "#241B33"
P = {
    "skin": "#FFEADF", "skin_sh": "#F4C6B6", "blush": "#FF8F98", "blush_line": "#E8606E",
    "hair": "#2E3373", "hair_sh": "#1D2154", "hair_hi": "#5E6FCB",
    "iris": "#E8892E", "iris_dk": "#A84B17", "iris_lt": "#FFC874", "pupil": "#4A1F0A",
    "eye_sh": "#E4DFF2",
    "cardigan": "#F6EEE2", "cardigan_sh": "#DFD0BC",
    "collar": "#2E3373", "collar_sh": "#1D2154",
    "phones": "#FF7457", "phones_sh": "#D2533D", "pad": "#3B2F52",
    "booklet": "#22306A", "booklet_sh": "#18214D", "gold": "#FFC062",
    "mouth": "#B8323D", "tongue": "#FF8A93", "tear": "#9BD8FF",
}
W_OUT = 3.6


def layer(label, body):
    return f'\n  <g inkscape:groupmode="layer" inkscape:label="{label}">{body}\n  </g>'


def fill(d, color, extra=""):
    return f'\n    <path d="{d}" fill="{color}"{extra}/>'


def line(d, w=W_OUT, color=INK, extra=""):
    return (f'\n    <path d="{d}" fill="none" stroke="{color}" stroke-width="{w}" '
            f'stroke-linejoin="round" stroke-linecap="round"{extra}/>')


def mirror_d(d):
    """Mirror a path about x = 200 (numbers alternate x, y)."""
    out, toks, is_x = [], d.replace(",", " ").split(), True
    for t in toks:
        try:
            v = float(t)
        except ValueError:
            out.append(t); is_x = True; continue
        out.append(f"{400 - v:g}" if is_x else f"{v:g}")
        is_x = not is_x
    return " ".join(out)


# ------------------------------------------------------------------ hair

BACK = ("M 96 222 C 82 150, 112 54, 200 50 C 288 54, 318 150, 304 222 "
        "C 302 254, 310 280, 304 304 C 294 300, 284 292, 276 284 L 124 284 "
        "C 116 292, 106 300, 96 304 C 90 280, 98 254, 96 222 Z")


def hair_back():
    shade = ("M 108 240 C 112 266, 120 278, 126 286 L 274 286 C 280 278, 288 266, 292 240 "
             "C 268 256, 132 256, 108 240 Z")
    return layer("hair-back", fill(BACK, P["hair"]) + fill(shade, P["hair_sh"]) + line(BACK))


CROWN = ("M 96 222 C 82 150, 112 54, 200 50 C 288 54, 318 150, 304 222 "
         "C 296 184, 284 160, 270 146 L 130 146 C 116 160, 104 184, 96 222 Z")
CROWN_TOP = "M 96 222 C 82 150, 112 54, 200 50 C 288 54, 318 150, 304 222"

# side lock in front of the cheek (left; the right one is its mirror)
LOCK = ("M 102 168 C 96 214, 106 270, 128 308 C 130 272, 132 238, 134 214 "
        "C 136 190, 140 168, 144 148 Z")
LOCK_OUT = "M 102 168 C 96 214, 106 270, 128 308"
LOCK_IN = "M 128 308 C 130 272, 132 238, 134 214 C 136 190, 140 168, 144 148"
LOCK_SPLIT = "M 118 196 C 116 228, 118 258, 124 284"

# fringe clumps: (root left, root right, tip), drawn outer first, centre last
CLUMPS = [
    ((110, 140), (164, 104), (132, 202)),
    ((290, 140), (236, 104), (268, 200)),
    ((136, 110), (198, 92), (164, 198)),
    ((264, 110), (202, 92), (238, 200)),
    ((168, 96), (234, 96), (198, 212)),
]


def _ctrl(a, b, bulge):
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    L = math.hypot(dx, dy) or 1
    nx, ny = -dy / L * bulge, dx / L * bulge
    return (a, (ax + dx * 0.35 + nx, ay + dy * 0.35 + ny), (ax + dx * 0.78 + nx * 0.55, ay + dy * 0.78 + ny * 0.55), b)


def _c(seg):
    _, c1, c2, b = seg
    return f"C {c1[0]:.1f} {c1[1]:.1f}, {c2[0]:.1f} {c2[1]:.1f}, {b[0]:.1f} {b[1]:.1f}"


def _split(seg, t0, t1):
    """The part of a cubic between t0 and t1 (de Casteljau, twice)."""
    def at(seg, t):
        p0, p1, p2, p3 = seg
        lerp = lambda a, b: (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        a, b, c = lerp(p0, p1), lerp(p1, p2), lerp(p2, p3)
        d, e = lerp(a, b), lerp(b, c)
        f = lerp(d, e)
        return (p0, a, d, f), (f, e, c, p3)
    _, right = at(seg, t0)
    t1n = (t1 - t0) / (1 - t0) if t0 < 1 else 1
    left, _ = at(right, t1n)
    return left


def _d(seg):
    return f"M {seg[0][0]:.1f} {seg[0][1]:.1f} " + _c(seg)


BULGE = 16


def clump_paths(rl, rr, tip):
    left, right = _ctrl(rl, tip, BULGE), _ctrl(tip, rr, BULGE)
    shape = (f"M {rl[0]} {rl[1]} " + _c(left) + " " + _c(right)
             + f" C {rr[0] - 8} {rr[1] - 14}, {rl[0] + 8} {rl[1] - 14}, {rl[0]} {rl[1]} Z")
    # ink only the lower part of each edge, so the clump melts into the crown
    return shape, _d(_split(left, 0.38, 1.0)), _d(_split(right, 0.0, 0.62)), _d(right)


def hair_front(uid):
    parts, clips = [], []
    # crown and locks
    lock_r = mirror_d(LOCK)
    base = fill(CROWN, P["hair"]) + fill(LOCK, P["hair"]) + fill(lock_r, P["hair"])
    base += fill("M 118 196 C 116 228, 118 258, 124 284 C 128 262, 130 236, 132 210 Z", P["hair_sh"])
    base += fill(mirror_d("M 102 168 C 96 214, 106 270, 128 308 C 122 270, 116 230, 114 190 Z"), P["hair_sh"])
    union = [CROWN, LOCK, lock_r]
    inks = ""
    for i, (ra, rb, tip) in enumerate(CLUMPS):
        rl, rr = sorted((ra, rb))
        shape, left, right, right_full = clump_paths(rl, rr, tip)
        union.append(shape)
        cid = f"{uid}-clump{i}"
        clips.append(f'<clipPath id="{cid}"><path d="{shape}"/></clipPath>')
        # shadow sliver inside the right-hand edge (light from upper left)
        shade = brush(right_full, 17, "both", P["hair_sh"], extra=f' clip-path="url(#{cid})"')
        # ink each clump right after its fill, so clumps in front hide the lines behind them
        parts.append(fill(shape, P["hair"]) + shade + brush(left, 4.4, "end") + brush(right, 4.4, "start"))
    # angel-ring highlight across everything, clipped to the hair
    uidh = f"{uid}-hair"
    clips.append(f'<clipPath id="{uidh}">' + "".join(f'<path d="{d}"/>' for d in union) + "</clipPath>")
    cx0, cy0 = 200, 30                         # hair flows out from here
    highlight = ""
    for k, ang in enumerate(range(-63, 64, 7)):
        if abs(ang) < 4:
            continue                                # a gap at the parting
        a = math.radians(ang)
        rm = 80 + abs(ang) * 0.30                   # ring follows the head's curve
        L = (19 if k % 2 else 15) - abs(ang) * 0.05 # half-length along the flow, alternating
        mx, my = cx0 + rm * math.sin(a), cy0 + rm * math.cos(a)
        ux, uy = math.sin(a), math.cos(a)
        vx, vy = uy, -ux
        w = 6.4 - abs(ang) * 0.03
        top, bot = (mx - ux * L, my - uy * L), (mx + ux * L, my + uy * L)
        d = (f"M {top[0]:.1f} {top[1]:.1f} Q {mx + vx * w:.1f} {my + vy * w:.1f} {bot[0]:.1f} {bot[1]:.1f} "
             f"Q {mx - vx * w:.1f} {my - vy * w:.1f} {top[0]:.1f} {top[1]:.1f} Z")
        highlight += fill(d, P["hair_hi"], f' clip-path="url(#{uidh})"')
    strands = ""
    for d, w in (("M 196 136 C 196 160, 194 182, 192 200", 1.8), ("M 152 140 C 150 160, 146 176, 142 190", 1.6),
                 ("M 250 140 C 252 160, 256 176, 260 190", 1.6)):
        strands += brush(d, w, "both", INK, extra=' fill-opacity="0.85"')
    # parting and a small ahoge
    ahoge = (fill("M 196 56 C 186 34, 202 18, 218 24 C 204 26, 198 36, 204 56 Z", P["hair"])
             + brush("M 196 56 C 186 34, 202 18, 218 24", 3.2, "end")
             + brush("M 218 24 C 204 26, 198 36, 204 56", 2.4, "start"))
    inks += line(CROWN_TOP)
    inks += brush(LOCK_OUT, 4.6, "end") + brush(LOCK_IN, 3.6, "both") + brush(LOCK_SPLIT, 2.2, "both")
    inks += brush(mirror_d(LOCK_OUT), 4.6, "end") + brush(mirror_d(LOCK_IN), 3.6, "both") + brush(mirror_d(LOCK_SPLIT), 2.2, "both")
    defs = "\n    <defs>" + "".join(clips) + "</defs>"
    return layer("hair-front", defs + base + "".join(parts) + highlight + strands + inks + ahoge)


# ------------------------------------------------------------------ face

FACE = ("M 126 140 C 118 180, 118 222, 136 250 C 154 276, 182 292, 200 293 "
        "C 218 292, 246 276, 264 250 C 282 222, 282 180, 274 140 Z")


def face():
    # shadow cast by the fringe: follows the clump tips, offset downwards
    tips = sorted(t for _, _, t in CLUMPS)  # left to right
    d = "M 120 140 L 280 140 L 278 176 "
    for (x, y) in reversed(tips):
        d += f"Q {x + 10} {y - 8} {x} {y + 9} "
    d += "L 122 176 Z"
    cheek = "M 272 204 C 270 232, 260 254, 242 272 C 258 264, 272 246, 278 214 Z"
    under_chin = "M 168 286 C 186 296, 214 296, 232 286 C 214 300, 186 300, 168 286 Z"
    return layer("face", fill(FACE, P["skin"]) + fill(d, P["skin_sh"]) + fill(cheek, P["skin_sh"])
                 + fill(under_chin, P["skin_sh"]) + line(FACE)
                 + brush("M 204 242 C 202 246, 200 248, 197 249", 2.2, "end"))   # nose


def eye(cx, cy, side, mood, uid, look=(0, 0)):
    s = side          # -1: viewer's left eye (outer corner left); +1: right eye
    lx, ly = look
    if mood == "happy":
        arc = f"M {cx - 21 * s} {cy + 5} C {cx - 10 * s} {cy - 13}, {cx + 10 * s} {cy - 13}, {cx + 21 * s} {cy + 5}"
        return brush(arc, 6.4, "lid") + brush(f"M {cx + 20 * s} {cy + 3} L {cx + 28 * s} {cy - 3}", 3.6, "end")
    white = (f"M {cx - 19} {cy - 6} C {cx - 16} {cy - 22}, {cx + 16} {cy - 24}, {cx + 19} {cy - 8} "
             f"C {cx + 21} {cy + 10}, {cx + 12} {cy + 24}, {cx} {cy + 24} "
             f"C {cx - 12} {cy + 24}, {cx - 21} {cy + 10}, {cx - 19} {cy - 6} Z")
    cid = f"{uid}-eye{'L' if s < 0 else 'R'}"
    ix, iy = cx + lx, cy + 2 + ly
    body = (f'\n    <defs><clipPath id="{cid}"><path d="{white}"/></clipPath></defs>'
            f'\n    <g clip-path="url(#{cid})">'
            f'\n      <path d="{white}" fill="#FFFFFF"/>'
            f'\n      <path d="M {cx - 24} {cy - 28} L {cx + 24} {cy - 28} L {cx + 24} {cy - 11} '
            f'C {cx + 8} {cy - 16}, {cx - 8} {cy - 16}, {cx - 24} {cy - 9} Z" fill="{P["eye_sh"]}"/>'
            f'\n      <ellipse cx="{ix}" cy="{iy}" rx="14.5" ry="20" fill="{P["iris"]}" stroke="{INK}" stroke-width="1.8"/>'
            f'\n      <path d="M {ix - 15} {iy - 5} C {ix - 8} {iy - 14}, {ix + 8} {iy - 14}, {ix + 15} {iy - 5} '
            f'L {ix + 15} {iy - 24} L {ix - 15} {iy - 24} Z" fill="{P["iris_dk"]}"/>'
            f'\n      <path d="M {ix - 10} {iy + 9} C {ix - 4} {iy + 17}, {ix + 6} {iy + 17}, {ix + 11} {iy + 8} '
            f'C {ix + 6} {iy + 12}, {ix - 4} {iy + 12}, {ix - 10} {iy + 9} Z" fill="{P["iris_lt"]}"/>'
            f'\n      <ellipse cx="{ix}" cy="{iy + 2}" rx="6.5" ry="10" fill="{P["pupil"]}"/>'
            f'\n      <ellipse cx="{ix - 6}" cy="{iy - 8}" rx="5.8" ry="7.4" fill="#FFFFFF"/>'
            f'\n      <circle cx="{ix + 6}" cy="{iy + 9}" r="2.6" fill="#FFFFFF"/>'
            f'\n    </g>')
    # upper lid drawn from the inner corner to the outer one, so the outer end is heaviest
    if s < 0:
        lid = f"M {cx + 21} {cy - 7} C {cx + 16} {cy - 26}, {cx - 16} {cy - 25}, {cx - 21} {cy - 4}"
        lash = f"M {cx - 19} {cy - 9} C {cx - 24} {cy - 12}, {cx - 27} {cy - 13}, {cx - 30} {cy - 12}"
    else:
        lid = f"M {cx - 21} {cy - 7} C {cx - 16} {cy - 26}, {cx + 16} {cy - 25}, {cx + 21} {cy - 4}"
        lash = f"M {cx + 19} {cy - 9} C {cx + 24} {cy - 12}, {cx + 27} {cy - 13}, {cx + 30} {cy - 12}"
    lower = f"M {cx - 7 * s} {cy + 26} C {cx + 2 * s} {cy + 27}, {cx + 9 * s} {cy + 26}, {cx + 14 * s} {cy + 22}"
    return body + brush(lid, 7.2, "lid") + brush(lash, 4.2, "end") + brush(lower, 2.6, "both")


def brows(mood):
    if mood == "sorry":
        L, R = "M 134 180 C 144 176, 156 170, 170 164", "M 266 180 C 256 176, 244 170, 230 164"
    elif mood == "busy":
        L, R = "M 136 170 C 146 165, 158 164, 170 167", "M 230 160 C 240 155, 252 157, 264 164"
    else:
        L, R = "M 136 172 C 146 167, 158 166, 170 169", "M 230 169 C 242 166, 254 167, 264 172"
    soft = ' fill-opacity="0.55"'
    return brush(L, 3.6, "both", INK, extra=soft) + brush(R, 3.6, "both", INK, extra=soft)


def blush(strength):
    out = (f'\n    <ellipse cx="157" cy="242" rx="17" ry="8" fill="{P["blush"]}" fill-opacity="{strength}"/>'
           f'\n    <ellipse cx="243" cy="242" rx="17" ry="8" fill="{P["blush"]}" fill-opacity="{strength}"/>')
    for x in (151, 159, 167, 235, 243, 251):
        out += brush(f"M {x} 237 L {x - 5} 246", 2.2, "both", P["blush_line"])
    return out


def mouth(mood):
    if mood == "happy":
        shape = "M 182 258 C 186 280, 214 280, 218 258 C 206 262, 194 262, 182 258 Z"
        return (fill(shape, P["mouth"]) + fill("M 190 271 C 196 278, 204 278, 210 271 C 206 266, 194 266, 190 271 Z", P["tongue"])
                + line(shape, 2.4))
    if mood == "busy":
        shape = "M 200 259 C 194 259, 193 271, 200 271 C 207 271, 206 259, 200 259 Z"
        return fill(shape, P["mouth"]) + line(shape, 2.2)
    if mood == "sorry":
        return brush("M 186 268 C 190 261, 194 261, 198 266 C 202 271, 206 271, 213 263", 3.0, "both")
    return brush("M 187 262 C 194 270, 206 270, 213 262", 3.2, "both")


def sparkle(x, y, r):
    d = (f"M {x} {y - r} Q {x + r * .2} {y - r * .2} {x + r} {y} Q {x + r * .2} {y + r * .2} {x} {y + r} "
         f"Q {x - r * .2} {y + r * .2} {x - r} {y} Q {x - r * .2} {y - r * .2} {x} {y - r} Z")
    return fill(d, P["gold"]) + line(d, 1.8)


def expression(mood, uid):
    if mood == "busy":
        eyes = eye(158, 212, -1, mood, uid, (4, -4)) + eye(242, 212, 1, mood, uid, (4, -4))
    elif mood == "sorry":
        eyes = eye(158, 214, -1, mood, uid, (0, 2)) + eye(242, 214, 1, mood, uid, (0, 2))
        tear = "M 141 240 C 137 250, 139 258, 145 260 C 150 256, 149 248, 141 240 Z"
        eyes += fill(tear, P["tear"]) + line(tear, 1.6)
    else:
        eyes = eye(158, 212, -1, mood, uid) + eye(242, 212, 1, mood, uid)
    extra = ""
    if mood == "happy":
        extra = sparkle(340, 84, 15) + sparkle(56, 112, 10) + sparkle(360, 140, 7)
    if mood == "busy":
        extra = brush("M 320 94 L 340 80", 4, "both") + brush("M 328 116 L 352 110", 4, "both")
    return (layer("eyes", eyes) + layer("mouth", blush(0.62 if mood == "happy" else 0.45) + mouth(mood)),
            layer("brows", brows(mood) + extra))


# ------------------------------------------------------------------ body

def body():
    cardigan = "M 34 400 C 44 352, 104 326, 166 318 L 234 318 C 296 326, 356 352, 366 400 Z"
    cardigan_sh = "M 238 320 C 298 328, 354 356, 364 400 L 304 400 C 298 372, 278 344, 238 320 Z"
    neck = "M 180 276 L 220 276 L 222 330 C 210 340, 190 340, 178 330 Z"
    neck_sh = "M 180 280 C 190 304, 210 304, 220 280 L 221 304 C 210 312, 190 312, 179 304 Z"
    collar = ("M 114 330 C 140 320, 162 316, 176 318 L 200 352 L 224 318 C 238 316, 260 320, 286 330 "
              "L 200 394 Z")
    collar_sh = "M 224 318 C 238 316, 260 320, 286 330 L 254 354 C 248 340, 238 326, 224 318 Z"
    band = "M 118 334 C 122 300, 278 300, 282 334"      # headphone band, behind the neck
    out = (fill(cardigan, P["cardigan"]) + fill(cardigan_sh, P["cardigan_sh"]) + line(cardigan)
           + brush("M 92 368 C 102 378, 108 390, 108 400", 2.2, "start")
           + brush("M 304 366 C 294 378, 290 390, 292 400", 2.2, "start")
           + line(band, 12) + line(band, 7.5, P["phones_sh"])
           + fill(neck, P["skin"]) + fill(neck_sh, P["skin_sh"]) + line(neck, 2.4)
           + fill(collar, P["collar"]) + fill(collar_sh, P["collar_sh"]) + line(collar, 2.6)
           + line("M 126 335 L 200 386 L 274 335", 3.2, "#FFFFFF")
           + line("M 176 318 L 200 352 L 224 318", 2.4))
    return layer("body", out)


def cup(cx, cy, rot, inner_right):
    k = 1 if inner_right else -1
    return (f'\n    <g transform="translate({cx} {cy}) rotate({rot})">'
            f'<rect x="-22" y="-28" width="44" height="56" rx="20" fill="{P["phones"]}"/>'
            f'<path d="M {6 * k} -27 C {24 * k} -22, {24 * k} 22, {6 * k} 27 L {22 * k} 18 L {22 * k} -18 Z" fill="{P["phones_sh"]}"/>'
            f'<rect x="-22" y="-28" width="44" height="56" rx="20" fill="none" stroke="{INK}" stroke-width="3"/>'
            f'<rect x="{2 if inner_right else -12}" y="-22" width="10" height="44" rx="5" fill="{P["pad"]}" stroke="{INK}" stroke-width="1.8"/>'
            f'<path d="M {-12 * k} -14 C {-14 * k} -4, {-14 * k} 6, {-11 * k} 15" fill="none" stroke="#FFFFFF" '
            f'stroke-opacity=".75" stroke-width="3.4" stroke-linecap="round"/>'
            f'</g>')


def accessories():
    return layer("accessories", cup(118, 348, -26, True) + cup(282, 348, 26, False))


def hair_clip():
    """A CD clipped to the side of her head, its face turned outwards (to the viewer's
    left), so it reads as an ellipse with its rim showing on the side nearer her head."""
    rx, ry = 12.5, 27.0
    return layer("hair-clip",
                 f'\n    <g transform="translate(93 132) rotate(24)">'
                 # the clip: a little silver clasp running from behind the disc into the hair
                 f'<path d="M 2 -6 L 26 -9 C 30 -9, 30 -1, 26 -1 L 2 2 Z" fill="#C9CFDC" stroke="{INK}" stroke-width="2.2" stroke-linejoin="round"/>'
                 f'<path d="M 8 -3 L 24 -5" stroke="#FFFFFF" stroke-opacity=".8" stroke-width="1.6" stroke-linecap="round"/>'
                 # rim: the back face peeking out, offset towards her head
                 f'<ellipse cx="4.5" cy="0" rx="{rx}" ry="{ry}" fill="#8E96AC" stroke="{INK}" stroke-width="3"/>'
                 # front face
                 f'<ellipse cx="0" cy="0" rx="{rx}" ry="{ry}" fill="url(#disc)"/>'
                 f'<ellipse cx="0" cy="0" rx="{rx}" ry="{ry}" fill="url(#rainbow)"/>'
                 f'<ellipse cx="0" cy="0" rx="{rx * .68:.1f}" ry="{ry * .68:.1f}" fill="none" stroke="#FFFFFF" stroke-opacity=".55" stroke-width="1.1"/>'
                 f'<ellipse cx="0" cy="0" rx="{rx}" ry="{ry}" fill="none" stroke="{INK}" stroke-width="3"/>'
                 f'<ellipse cx="0" cy="0" rx="{rx * .32:.1f}" ry="{ry * .32:.1f}" fill="#E9ECF3" stroke="{INK}" stroke-width="1.6"/>'
                 f'<ellipse cx="0.8" cy="0" rx="{rx * .14:.1f}" ry="{ry * .14:.1f}" fill="{P["hair_sh"]}"/>'
                 # specular glint along the upper-left of the face
                 f'<path d="M {-rx * .62:.1f} {-ry * .45:.1f} C {-rx * .55:.1f} {-ry * .78:.1f}, {-rx * .2:.1f} {-ry * .92:.1f}, {rx * .15:.1f} {-ry * .88:.1f}" '
                 f'fill="none" stroke="#FFFFFF" stroke-width="3" stroke-linecap="round"/>'
                 f'</g>')


# Forearms rise from below the frame; the hands hold the booklet's lower corners with
# the fingers spread over its front. Both arms share one local drawing: +x runs along
# the forearm towards the booklet, the wrist sits at the origin.
ARMS = (("translate(150 372) rotate(-34) scale(1.1)"), ("translate(257 361) scale(-1.1 1.1) rotate(-34)"))
SLEEVE = "M -96 -23 L -4 -18 C 3 -18, 3 18, -4 18 L -96 26 Z"
SLEEVE_SH = "M -96 12 L -4 9 C 2 10, 3 14, -4 18 L -96 26 Z"
CUFF = "M -13 -18.6 L -4 -18 C 3 -18, 3 18, -4 18 L -13 18.6 C -9 6, -9 -6, -13 -18.6 Z"
HAND = ("M -2 -11 C 6 -13, 13 -15, 21 -14 L 31 -13 C 39 -12, 42 -6, 40 -1 "
        "C 42 5, 40 12, 32 13 L 21 14 C 13 15, 6 13, -2 11 Z")
HAND_SH = ("M 4 8 C 12 11, 22 12, 31 11 C 36 10, 39 8, 40 5 C 41 9, 38 12, 32 13 L 21 14 "
           "C 13 15, 6 13, -2 11 Z")


def arms():
    out = ""
    for t in ARMS:
        out += (f'\n    <g transform="{t}">' + fill(SLEEVE, P["cardigan"]) + fill(SLEEVE_SH, P["cardigan_sh"])
                + line(SLEEVE, 3.2) + "</g>")
    return layer("arms", out)


def hands():
    out = ""
    for t in ARMS:
        out += (f'\n    <g transform="{t}">'
                + fill(HAND, P["skin"]) + fill(HAND_SH, P["skin_sh"]) + line(HAND, 2.6)
                + brush("M 25 -6 C 30 -6, 35 -6, 39 -5", 2.0, "start")      # fingers
                + brush("M 25 1 C 30 1, 35 1, 40 1", 2.0, "start")
                + brush("M 25 7 C 30 7, 34 7, 38 8", 2.0, "start")
                + brush("M 12 -8 C 15 -10, 18 -10, 21 -9", 1.6, "both")     # knuckle
                # the cuff goes over the wrist
                + fill(CUFF, P["cardigan_sh"]) + line(CUFF, 2.6)
                + brush("M -9 -12 L -9 12", 1.4, "both") + "</g>")
    return layer("hands", out)


def booklet():
    book = (f'\n    <g transform="translate(0 -12) rotate(-6 200 382)">'
            f'<rect x="158" y="342" width="86" height="76" rx="5" fill="{P["booklet"]}"/>'
            f'<path d="M 228 344 L 242 344 L 242 418 L 228 418 Z" fill="{P["booklet_sh"]}"/>'
            f'<rect x="164" y="348" width="74" height="64" rx="3" fill="none" stroke="#FFFFFF" stroke-opacity=".3" stroke-width="1.6"/>'
            f'<rect x="158" y="342" width="86" height="76" rx="5" fill="none" stroke="{INK}" stroke-width="3"/>'
            f'<text x="203" y="388" text-anchor="middle" font-family="DejaVu Sans, Segoe UI Symbol, sans-serif" '
            f'font-size="36" fill="{P["gold"]}" stroke="{INK}" stroke-width="1.4">&#9834;</text>'
            f'</g>')
    return layer("booklet", book)


def defs(uid):
    return f"""
  <defs>
    <radialGradient id="disc" cx="0.38" cy="0.35" r="0.75">
      <stop offset="0" stop-color="#FFFFFF"/><stop offset="0.55" stop-color="#DDE2EC"/><stop offset="1" stop-color="#A9B1C4"/>
    </radialGradient>
    <linearGradient id="rainbow" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0.15" stop-color="#7FD4FF" stop-opacity="0"/><stop offset="0.35" stop-color="#7FD4FF" stop-opacity=".75"/>
      <stop offset="0.5" stop-color="#C69BFF" stop-opacity=".75"/><stop offset="0.65" stop-color="#FFB3D1" stop-opacity=".75"/>
      <stop offset="0.85" stop-color="#FFE38A" stop-opacity="0"/>
    </linearGradient>
  </defs>"""


MOODS = ["idle", "busy", "happy", "sorry"]


def draw(mood, size=400):
    uid = f"noto-{mood}"
    face_layers, brow_layer = expression(mood, uid)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
            f'viewBox="0 0 400 400" width="{size}" height="{size}">'
            f'\n  <title>N&#333;to, the LinerNotes mascot ({mood})</title>{defs(uid)}'
            + hair_back() + body() + accessories() + face() + face_layers
            + hair_front(uid) + brow_layer + hair_clip() + arms() + booklet() + hands()
            + "\n</svg>\n")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for m in MOODS:
        with open(f"{out}/noto-{m}.svg", "w", encoding="utf-8") as f:
            f.write(draw(m))
    print("wrote", ", ".join(f"noto-{m}.svg" for m in MOODS))
