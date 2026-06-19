#!/usr/bin/env python3
"""Convert source PNG artwork into 42x42 RGB565 icon headers for the launcher.

Place source images in tools/icon_src/ named:
    stopwatch.png, timer.png, pomodoro.png

Each is downscaled to 42x42. Fully transparent pixels become the launcher's
transparent colour key (0x0000 = TFT_BLACK); opaque pixels are composited over
black and converted to RGB565. Opaque pixels that would land on pure black are
nudged to a near-black value so they are NOT keyed out (keeps black outlines).
"""
import os
from PIL import Image

W = H = 42
ALPHA_CUTOFF = 90        # below this alpha -> transparent
NEAR_BLACK = 0x0841      # ~(8,8,8): opaque "black" that survives the transparency key

# map output icon -> list of accepted source filenames (first found wins)
SOURCES = {
    "icon_stopwatch": ["stopwatch.png"],
    "icon_timer":     ["timer.png", "hourglass.png"],
    "icon_pomodoro":  ["pomodoro.png", "tomato.png"],
}


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def swap16(v):
    return ((v & 0xFF) << 8) | (v >> 8)


def convert(img):
    img = img.convert("RGBA").resize((W, H), Image.LANCZOS)
    px = img.load()
    out = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a < ALPHA_CUTOFF:
                out.append(0x0000)
                continue
            # composite over black using the pixel's alpha
            r = r * a // 255
            g = g * a // 255
            b = b * a // 255
            v = rgb565(r, g, b)
            if v == 0x0000:
                v = NEAR_BLACK
            # launcher icons are stored byte-swapped (big-endian) RGB565
            out.append(swap16(v))
    return out


def emit(name, vals):
    lines = ["#pragma once", "#include <stdint.h>", "",
             f"static const uint16_t image_data_{name}[{W * H}] = {{"]
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


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    src_dir = os.path.join(here, "icon_src")
    out_dir = os.path.normpath(os.path.join(here, "..", "src", "apps", "launcher", "launcher_icons"))

    missing = []
    for icon, fnames in SOURCES.items():
        path = next((os.path.join(src_dir, f) for f in fnames
                     if os.path.isfile(os.path.join(src_dir, f))), None)
        if path is None:
            missing.append(f"{icon} (looked for: {', '.join(fnames)})")
            continue
        vals = convert(Image.open(path))
        with open(os.path.join(out_dir, icon + ".h"), "w") as f:
            f.write(emit(icon, vals))
        print("wrote", icon + ".h", "from", os.path.basename(path))

    if missing:
        print("\nMISSING source images:")
        for m in missing:
            print("  ", m)


if __name__ == "__main__":
    main()
