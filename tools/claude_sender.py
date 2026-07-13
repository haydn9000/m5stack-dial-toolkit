#!/usr/bin/env python3
"""
claude_sender.py — Sends Claude API usage data to the M5Dial over USB serial or BLE.

Polls the Anthropic API every POLL_INTERVAL seconds and writes a compact JSON
line to the M5Dial. The M5Dial's PC_LINK channel reads this and updates the
Claude Usage app.

Usage:
    python claude_sender.py COM3           # USB serial — Windows
    python claude_sender.py /dev/ttyACM0   # USB serial — Linux/macOS
    python claude_sender.py --ble          # BLE (auto-discovers M5DIAL-001)
    python claude_sender.py --ble AA:BB:CC:DD:EE:FF  # BLE to specific address

Requirements:
    pip install httpx pyserial

Optional:
    pip install bleak   # BLE transport (required for --ble mode only)

Credentials:
    Reads the same OAuth token as Claude Code:
    - Windows/Linux: ~/.claude/.credentials.json  (field: "accessToken")
    - macOS:         macOS Keychain entry "Claude Code-credentials" (via safeStorage)
"""

import json
import re
import sys
import time
from datetime import datetime
from pathlib import Path

import httpx

# ---- optional: BLE via bleak (only needed for --ble mode) ----
_bleak_available = False
try:
    import asyncio
    from bleak import BleakClient, BleakScanner
    _bleak_available = True
except ImportError:
    pass

# ---- Configuration -------------------------------------------------------
POLL_INTERVAL = 60       # seconds between API polls
BAUD_RATE     = 115200   # must match Serial.begin() on the M5Dial

BLE_DEVICE_NAME  = "M5DIAL-001"
BLE_RX_CHAR_UUID = "4d354449-414c-0000-0000-000000000002"

# ---- Anthropic API -------------------------------------------------------
API_URL  = "https://api.anthropic.com/v1/messages"
API_BODY = {
    "model": "claude-haiku-4-5-20251001",
    "max_tokens": 1,
    "messages": [{"role": "user", "content": "hi"}],
}
API_HEADERS_TEMPLATE = {
    "anthropic-version": "2023-06-01",
    "anthropic-beta":    "oauth-2025-04-20",
    "Content-Type":      "application/json",
    "User-Agent":        "claude-code/2.1.5",
}


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def format_reset_time(reset_ts: str) -> str:
    """Convert a Unix timestamp to a local-time display string."""
    try:
        ts = float(reset_ts)
    except (ValueError, TypeError):
        return ""
    dt      = datetime.fromtimestamp(ts)
    now     = datetime.now()
    tz_abbr = datetime.now().astimezone().strftime('%Z')
    hour    = dt.hour % 12 or 12
    ampm    = "am" if dt.hour < 12 else "pm"
    mins    = f":{dt.minute:02d}" if dt.minute != 0 else ""
    time_s  = f"{hour}{mins}{ampm}"
    if dt.date() == now.date():
        return f"Resets {time_s} ({tz_abbr})"
    months = ["Jan","Feb","Mar","Apr","May","Jun",
              "Jul","Aug","Sep","Oct","Nov","Dec"]
    return f"Resets {months[dt.month - 1]} {dt.day}, {time_s} ({tz_abbr})"


def _token_from_blob(blob: str) -> str | None:
    """Extract accessToken from a JSON blob (handles nested structures)."""
    try:
        data = json.loads(blob)
        if isinstance(data.get("accessToken"), str):
            return data["accessToken"]
        for v in data.values():
            if isinstance(v, dict) and isinstance(v.get("accessToken"), str):
                return v["accessToken"]
    except (json.JSONDecodeError, AttributeError):
        pass
    m = re.search(r'"accessToken"\s*:\s*"([^"]+)"', blob)
    return m.group(1) if m else None


def load_token() -> str | None:
    """Load the OAuth access token.

    Search order:
      1. ~/.claude/.credentials.json          — Windows / Linux flat file
      2. macOS Keychain "Claude Code-credentials" — macOS Claude Code storage
    """
    creds_path = Path.home() / ".claude" / ".credentials.json"
    if creds_path.exists():
        token = _token_from_blob(creds_path.read_text().strip())
        if token:
            return token

    if sys.platform == "darwin":
        try:
            import subprocess
            result = subprocess.run(
                ["security", "find-generic-password", "-s", "Claude Code-credentials", "-w"],
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0 and result.stdout.strip():
                token = _token_from_blob(result.stdout.strip())
                if token:
                    return token
        except Exception:
            pass

    log("Could not load API token — aborting.")
    log("  On Windows/Linux: ensure ~/.claude/.credentials.json exists (log in to Claude Code or the Claude desktop app).")
    log("  On macOS:         ensure you are logged in to Claude Code (token stored in Keychain).")
    return None


def poll_api(token: str) -> dict | None:
    """Poll the Anthropic API and return a compact usage payload dict."""
    headers = {**API_HEADERS_TEMPLATE, "Authorization": f"Bearer {token}"}
    try:
        resp = httpx.post(API_URL, json=API_BODY, headers=headers, timeout=15)
    except httpx.RequestError as e:
        log(f"API request failed: {e}")
        return None

    if resp.status_code == 401:
        log("Token expired or rejected (401) — credentials will be reloaded on next poll.")
        return None
    if resp.status_code not in (200, 429):
        log(f"Unexpected status {resp.status_code}")
        return None

    def hdr(name: str, default: str = "0") -> str:
        return resp.headers.get(name, default)

    now = time.time()

    def reset_minutes(ts: str) -> int:
        try:
            r = float(ts)
        except ValueError:
            return 0
        return max(0, int(round((r - now) / 60.0)))

    def pct(util: str) -> int:
        try:
            return int(round(float(util) * 100))
        except ValueError:
            return 0

    sr_ts = hdr("anthropic-ratelimit-unified-5h-reset", "0")
    wr_ts = hdr("anthropic-ratelimit-unified-7d-reset", "0")

    return {
        "s":   pct(hdr("anthropic-ratelimit-unified-5h-utilization")),
        "sr":  reset_minutes(sr_ts),
        "w":   pct(hdr("anthropic-ratelimit-unified-7d-utilization")),
        "wr":  reset_minutes(wr_ts),
        "st":  hdr("anthropic-ratelimit-unified-5h-status", "unknown"),
        "srt": format_reset_time(sr_ts),
        "wrt": format_reset_time(wr_ts),
        "ok":  True,
    }


# ---- BLE transport -------------------------------------------------------

async def _ble_find(address: str | None):
    if address:
        log(f"Using address: {address}")
        return address
    log(f'Scanning for "{BLE_DEVICE_NAME}"...')
    device = await BleakScanner.find_device_by_name(BLE_DEVICE_NAME, timeout=10.0)
    if device:
        log(f"Found {device.name} at {device.address}")
        return device
    log(f'"{BLE_DEVICE_NAME}" not found — is the M5Dial on the Claude Usage screen?')
    return None


async def _ble_run(address: str | None) -> None:
    token = ""
    while True:
        target = await _ble_find(address)
        if not target:
            log("Retrying scan in 10s...")
            await asyncio.sleep(10)
            continue
        addr = target if isinstance(target, str) else target.address

        try:
            # use_cached_services=False: Windows/WinRT caches each BLE address's
            # GATT table. This device's address is shared with other apps on the
            # M5Dial (BLE Volume, BLE Server) that advertise different GATT
            # profiles, so a stale cached table can hide our characteristic —
            # force a fresh service discovery every connection.
            async with BleakClient(target, winrt={"use_cached_services": False}) as client:
                log(f"Connected to {addr}")
                last_poll = 0.0

                while client.is_connected:
                    now = time.time()
                    if now - last_poll >= POLL_INTERVAL:
                        token = load_token() or token
                        log("Polling Anthropic API...")
                        payload = poll_api(token)
                        if payload:
                            line = json.dumps(payload, separators=(",", ":")) + "\n"
                            data = line.encode()
                            await client.write_gatt_char(BLE_RX_CHAR_UUID, data, response=True)
                            log(f"Sent ({len(data)}B): {line.strip()}")
                        else:
                            log("Poll failed — will retry next interval.")
                        last_poll = time.time()
                    await asyncio.sleep(1)

                log("Disconnected.")
        except Exception as e:
            log(f"BLE error: {e}")

        log("Reconnecting in 5s...")
        await asyncio.sleep(5)


# ---- Entry point ---------------------------------------------------------

def main() -> None:
    args = sys.argv[1:]

    if args and args[0] == "--ble":
        if not _bleak_available:
            print("BLE mode requires bleak:  pip install bleak")
            sys.exit(1)
        address = args[1] if len(args) > 1 else None
        if not load_token():
            sys.exit(1)
        log("Token loaded.")
        try:
            asyncio.run(_ble_run(address))
        except KeyboardInterrupt:
            log("Stopped.")
        return

    if not args:
        print(f"Usage: python {sys.argv[0]} <port>")
        print(f"       python {sys.argv[0]} --ble [address]")
        print( "  e.g. python claude_sender.py COM3")
        print( "  e.g. python claude_sender.py --ble")
        sys.exit(1)

    import serial

    port = args[0]
    if not load_token():
        sys.exit(1)
    log("Token loaded.")

    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
        log(f"Opened {port} at {BAUD_RATE} baud.")
    except serial.SerialException as e:
        log(f"Could not open {port}: {e}")
        sys.exit(1)

    token = ""
    last_poll = 0.0
    try:
        while True:
            now = time.time()
            if now - last_poll >= POLL_INTERVAL:
                token = load_token() or token
                log("Polling Anthropic API...")
                payload = poll_api(token)
                if payload:
                    line = json.dumps(payload, separators=(",", ":")) + "\n"
                    ser.write(line.encode())
                    log(f"Sent: {line.strip()}")
                    last_poll = now
                else:
                    log("Poll failed — will retry next interval.")
                    last_poll = now
            time.sleep(1)
    except KeyboardInterrupt:
        log("Stopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
