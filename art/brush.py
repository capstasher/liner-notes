"""A tiny vector 'brush': turns a path centre-line into a filled, tapered stroke.

Supports M, C, Q, L (absolute) in the input. Output is a closed polygon path with
enough samples to look smooth at any size the mascot is drawn.
"""
import math
import re

_TOK = re.compile(r"[MCQLZ]|-?\d*\.?\d+(?:e-?\d+)?")


def parse(d):
    """-> list of cubic segments ((x0,y0),(x1,y1),(x2,y2),(x3,y3))."""
    toks = _TOK.findall(d.replace(",", " "))
    segs, i, cur, cmd = [], 0, None, None
    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v
    while i < len(toks):
        t = toks[i]
        if t in "MCQLZ":
            cmd = t; i += 1
            if cmd == "Z":
                continue
        if cmd == "M":
            cur = (num(), num()); cmd = "L"     # implicit lineto after moveto
            continue
        if cmd == "C":
            p1 = (num(), num()); p2 = (num(), num()); p3 = (num(), num())
            segs.append((cur, p1, p2, p3)); cur = p3
        elif cmd == "Q":
            q = (num(), num()); p3 = (num(), num())
            p1 = (cur[0] + 2 / 3 * (q[0] - cur[0]), cur[1] + 2 / 3 * (q[1] - cur[1]))
            p2 = (p3[0] + 2 / 3 * (q[0] - p3[0]), p3[1] + 2 / 3 * (q[1] - p3[1]))
            segs.append((cur, p1, p2, p3)); cur = p3
        elif cmd == "L":
            p3 = (num(), num())
            p1 = (cur[0] + (p3[0] - cur[0]) / 3, cur[1] + (p3[1] - cur[1]) / 3)
            p2 = (cur[0] + 2 * (p3[0] - cur[0]) / 3, cur[1] + 2 * (p3[1] - cur[1]) / 3)
            segs.append((cur, p1, p2, p3)); cur = p3
    return segs


def _pt(seg, t):
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = seg
    u = 1 - t
    return (u ** 3 * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t ** 3 * x3,
            u ** 3 * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t ** 3 * y3)


def sample(d, per_seg=28):
    pts = []
    for k, seg in enumerate(parse(d)):
        for j in range(per_seg + 1):
            if k and j == 0:
                continue
            pts.append(_pt(seg, j / per_seg))
    return pts


def profile(kind, t):
    """Width multiplier along the stroke, t in [0,1]."""
    if kind == "both":                  # thin - thick - thin, like a pen flick
        return max(0.0, math.sin(math.pi * t)) ** 0.7
    if kind == "start":                 # thin at the start, full at the end
        return min(1.0, (t / 0.6)) ** 0.8
    if kind == "end":                   # full at the start, thin at the end
        return min(1.0, ((1 - t) / 0.6)) ** 0.8
    if kind == "lid":                   # eyelids: thin inner corner, heavy outer third
        return 0.35 + 0.65 * (t ** 1.2)
    return 1.0


def brush(d, width, kind="both", color="#241B33", min_w=0.35, extra=""):
    pts = sample(d)
    n = len(pts)
    if n < 2:
        return ""
    left, right = [], []
    for i, (x, y) in enumerate(pts):
        xa, ya = pts[max(0, i - 1)]
        xb, yb = pts[min(n - 1, i + 1)]
        dx, dy = xb - xa, yb - ya
        L = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / L, dx / L
        w = max(min_w, width * profile(kind, i / (n - 1))) / 2
        left.append((x + nx * w, y + ny * w))
        right.append((x - nx * w, y - ny * w))
    poly = left + right[::-1]
    d_out = "M " + " L ".join(f"{x:.2f} {y:.2f}" for x, y in poly) + " Z"
    return f'\n    <path d="{d_out}" fill="{color}"{extra}/>'
