#!/usr/bin/env python3
"""Drive an M5Dial running a -DDEV_REMOTE_ENABLE build over USB serial.

    python tools/dial_remote.py COM9 shot out.png       # screenshot
    python tools/dial_remote.py COM9 turn 3             # 3 detents clockwise
    python tools/dial_remote.py COM9 press 100          # short press
    python tools/dial_remote.py COM9 press 900          # long press (exit app)
    python tools/dial_remote.py COM9 mem                # free internal heap
    python tools/dial_remote.py COM9 ble                # NimBLE: connections, advertising
    python tools/dial_remote.py COM9 "turn 2" "press 100" "shot a.png"

Several commands run in order on one connection; each non-shot command is
followed by a short settle delay so the UI can react before the next one.
Screenshots are rendered inside a circular mask, as the panel shows them.
"""
import sys
import time
import serial


def read_shot(ser, path, timeout=5.0):
    from PIL import Image, ImageDraw
    deadline = time.time() + timeout
    header = b""
    while not header.startswith(b"~SHOT"):
        if time.time() > deadline:
            raise TimeoutError("no ~SHOT reply (is this a -DDEV_REMOTE_ENABLE build?)")
        header = ser.readline()
    _, w, h = header.decode().split()
    w, h = int(w), int(h)
    data = b""
    while len(data) < w * h * 2:
        chunk = ser.read(w * h * 2 - len(data))
        if not chunk and time.time() > deadline:
            raise TimeoutError(f"short screenshot: {len(data)} bytes")
        data += chunk
    img = Image.new("RGB", (w, h))
    px = img.load()
    for i in range(w * h):
        v = (data[2 * i] << 8) | data[2 * i + 1]          # big-endian RGB565
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        px[i % w, i // w] = (r * 255 // 31, g * 255 // 63, b * 255 // 31)
    # Round, antialiased mask (drawn at 4x, then downsampled); outside the
    # panel is transparent so the PNG drops straight into docs.
    mask = Image.new("L", (w * 4, h * 4), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, w * 4 - 1, h * 4 - 1), fill=255)
    out = img.convert("RGBA")
    out.putalpha(mask.resize((w, h), Image.LANCZOS))
    out.save(path)
    print("saved", path)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    port, args = sys.argv[1], sys.argv[2:]
    # Either one command split across argv ("shot out.png") or several quoted.
    cmds = [" ".join(args)] if " " not in args[0] else args
    # Open with DTR/RTS deasserted: toggling them resets the ESP32-S3 over
    # its USB-Serial-JTAG port, which would wipe the screen we want to see.
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0.5
    ser.dtr, ser.rts = False, False
    with ser:
        time.sleep(0.1)
        for cmd in cmds:
            parts = cmd.split()
            if parts[0] == "shot":
                # The first bytes after the port opens can be dropped while
                # USB settles, so retry a couple of times before giving up.
                for attempt in range(3):
                    ser.reset_input_buffer()
                    ser.write(b"~shot\n")
                    try:
                        read_shot(ser, parts[1] if len(parts) > 1 else "shot.png",
                                  timeout=2.0 if attempt < 2 else 5.0)
                        break
                    except TimeoutError:
                        if attempt == 2:
                            raise
            elif parts[0] in ("mem", "ble"):
                ser.reset_input_buffer()
                ser.write(("~" + parts[0] + "\n").encode())
                deadline = time.time() + 3
                while time.time() < deadline:
                    line = ser.readline()
                    if line.startswith(b"~" + parts[0].upper().encode()):
                        print(line.decode().strip())
                        break
            elif parts[0] == "wait":
                time.sleep(float(parts[1]))
            else:
                ser.write(("~" + cmd + "\n").encode())
                ms = int(parts[1]) if parts[0] == "press" else 0
                time.sleep(0.35 + ms / 1000.0)


if __name__ == "__main__":
    main()
