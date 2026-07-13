#!/usr/bin/env python3
"""Generates the Claude Usage launcher icon (42x42 RGB565)."""
import math

W = H = 42

def color565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def new_canvas():
    return [0x0000] * (W * H)

def set_px(buf, x, y, col):
    x, y = round(x), round(y)
    if 0 <= x < W and 0 <= y < H:
        buf[y * W + x] = col

def draw_line(buf, x0, y0, x1, y1, col, thickness=1):
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) * 2 + 1
    for i in range(steps + 1):
        t = i / steps
        x = x0 + (x1 - x0) * t
        y = y0 + (y1 - y0) * t
        for ox in range(-(thickness // 2), thickness - thickness // 2):
            for oy in range(-(thickness // 2), thickness - thickness // 2):
                set_px(buf, x + ox, y + oy, col)

def emit(name, buf):
    print(f"static const uint16_t image_data_icon_{name}[{W*H}] = {{")
    for row in range(H):
        vals = ", ".join(f"0x{v:04X}" for v in buf[row*W:(row+1)*W])
        print(f"    {vals},")
    print("};")

claude = new_canvas()
cx, cy, r = 21, 21, 15
col = color565(217, 119, 87)   # coral, matches CLAUDE_ACCENT 0xD97757
for k in range(3):
    ang = math.radians(k * 60)
    x0 = cx - r * math.cos(ang); y0 = cy - r * math.sin(ang)
    x1 = cx + r * math.cos(ang); y1 = cy + r * math.sin(ang)
    draw_line(claude, x0, y0, x1, y1, col, thickness=3)
emit("claude", claude)

pcstats = new_canvas()
bars = [(10, 26, color565(0, 240, 255)),    # cyan
        (20, 18, color565(80, 255, 100)),   # green
        (30, 32, color565(240, 40, 200))]   # magenta
base_y = 34
for x, h, bcol in bars:
    for yy in range(base_y - h, base_y):
        for xx in range(x, x + 8):
            set_px(pcstats, xx, yy, bcol)
emit("pcstats", pcstats)
