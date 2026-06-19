#!/usr/bin/env python3
"""
Generate 42x42 RGB565 cyberpunk icon headers for the new time apps.

Outputs three headers in src/apps/launcher/launcher_icons/ matching the
existing icon format (static const uint16_t image_data_icon_xxx[1764]).
Pure-python rasteriser, no external dependencies.
"""
import math
import os

W = H = 42

# --- palette (R, G, B) ---
# BG must be pure black: the launcher draws icons with TFT_BLACK (0x0000) as the
# transparent colour key, so any non-black background would show as a dark square.
BG      = (0, 0, 0)
CYAN    = (0, 229, 255)
CYAN_D  = (0, 120, 150)
AMBER   = (255, 179, 0)
AMBER_D = (150, 100, 0)
RED     = (255, 51, 85)
RED_D   = (150, 25, 45)
GREEN   = (39, 255, 153)
WHITE   = (230, 251, 255)


def new_canvas():
    return [[BG for _ in range(W)] for _ in range(H)]


def put(c, x, y, col):
    if 0 <= x < W and 0 <= y < H:
        c[int(y)][int(x)] = col


def fill_disc(c, cx, cy, r, col):
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                put(c, x, y, col)


def ring(c, cx, cy, r, thick, col):
    r2o = r * r
    r2i = (r - thick) * (r - thick)
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            d = (x - cx) ** 2 + (y - cy) ** 2
            if r2i <= d <= r2o:
                put(c, x, y, col)


def fill_rect(c, x0, y0, x1, y1, col):
    for y in range(int(y0), int(y1) + 1):
        for x in range(int(x0), int(x1) + 1):
            put(c, x, y, col)


def line(c, x0, y0, x1, y1, thick, col):
    steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
    for i in range(steps + 1):
        t = i / steps
        x = x0 + (x1 - x0) * t
        y = y0 + (y1 - y0) * t
        fill_disc(c, x, y, thick / 2.0, col)


def fill_tri(c, p0, p1, p2, col):
    xs = [p0[0], p1[0], p2[0]]
    ys = [p0[1], p1[1], p2[1]]
    minx, maxx = int(min(xs)), int(max(xs)) + 1
    miny, maxy = int(min(ys)), int(max(ys)) + 1

    def sign(ax, ay, bx, by, px, py):
        return (px - bx) * (ay - by) - (ax - bx) * (py - by)

    for y in range(miny, maxy + 1):
        for x in range(minx, maxx + 1):
            d1 = sign(p0[0], p0[1], p1[0], p1[1], x + 0.5, y + 0.5)
            d2 = sign(p1[0], p1[1], p2[0], p2[1], x + 0.5, y + 0.5)
            d3 = sign(p2[0], p2[1], p0[0], p0[1], x + 0.5, y + 0.5)
            has_neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
            has_pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
            if not (has_neg and has_pos):
                put(c, x, y, col)


def rgb565(col):
    r, g, b = col
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def emit(name, canvas):
    vals = [rgb565(canvas[y][x]) for y in range(H) for x in range(W)]
    lines = []
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"static const uint16_t image_data_{name}[{W * H}] = {{")
    row = "    "
    for i, v in enumerate(vals):
        row += f"0x{v:04X}, "
        if (i + 1) % 12 == 0:
            lines.append(row.rstrip())
            row = "    "
    if row.strip():
        lines.append(row.rstrip())
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


# ---------------- Stopwatch ----------------
def make_stopwatch():
    c = new_canvas()
    cx, cy = 21, 24
    # top push button + stem
    fill_rect(c, 18, 3, 24, 7, CYAN)
    fill_rect(c, 20, 6, 22, 10, CYAN)
    # side start/stop lug (top-right)
    line(c, 31, 12, 34, 9, 3, CYAN)
    # outer dial ring
    ring(c, cx, cy, 15, 3, CYAN)
    ring(c, cx, cy, 15, 1, WHITE)
    # tick marks N/E/S/W
    for ang in (0, 90, 180, 270):
        a = math.radians(ang - 90)
        x = cx + math.cos(a) * 11
        y = cy + math.sin(a) * 11
        fill_disc(c, x, y, 1.2, CYAN_D)
    # hand pointing up-right
    line(c, cx, cy, cx + 8, cy - 8, 2, WHITE)
    line(c, cx, cy, cx - 4, cy + 4, 2, CYAN)
    # hub
    fill_disc(c, cx, cy, 2.4, WHITE)
    return c


# ---------------- Timer (hourglass) ----------------
def make_timer():
    c = new_canvas()
    # top & bottom caps
    fill_rect(c, 9, 5, 33, 8, AMBER)
    fill_rect(c, 9, 34, 33, 37, AMBER)
    # glass outline: top triangle (points down) + bottom triangle (points up)
    top = [(11, 9), (31, 9), (21, 21)]
    bot = [(21, 21), (11, 33), (31, 33)]
    # glass body (dim)
    fill_tri(c, *top, AMBER_D)
    fill_tri(c, *bot, AMBER_D)
    # sand: top remaining (upper band) and bottom pile
    fill_tri(c, (12, 10), (30, 10), (21, 17), AMBER)
    fill_tri(c, (21, 28), (14, 33), (28, 33), AMBER)
    # falling stream
    line(c, 21, 19, 21, 27, 1.4, WHITE)
    # edges
    line(c, 11, 9, 31, 9, 1.4, WHITE)
    line(c, 11, 33, 31, 33, 1.4, WHITE)
    return c


# ---------------- Pomodoro (tomato) ----------------
def make_pomodoro():
    c = new_canvas()
    cx, cy = 21, 25
    fill_disc(c, cx, cy, 15, RED)
    fill_disc(c, cx, cy, 15.0, RED)
    # subtle dark shading lower-right
    fill_disc(c, cx + 4, cy + 4, 9, RED_D)
    fill_disc(c, cx, cy, 12, RED)
    # glossy highlight upper-left
    fill_disc(c, cx - 5, cy - 5, 3, WHITE)
    # leafy green crown
    for dx in (-7, -3, 0, 3, 7):
        fill_tri(c, (cx, cy - 9), (cx + dx, cy - 16), (cx + dx + 3, cy - 9), GREEN)
    fill_disc(c, cx, cy - 9, 3, GREEN)
    # stem
    line(c, cx, cy - 9, cx, cy - 15, 1.6, GREEN)
    return c


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = os.path.normpath(os.path.join(here, "..", "src", "apps", "launcher", "launcher_icons"))
    targets = {
        "icon_stopwatch": make_stopwatch(),
        "icon_timer": make_timer(),
        "icon_pomodoro": make_pomodoro(),
    }
    for name, canvas in targets.items():
        path = os.path.join(out_dir, name + ".h")
        with open(path, "w") as f:
            f.write(emit(name, canvas))
        print("wrote", path)


if __name__ == "__main__":
    main()
