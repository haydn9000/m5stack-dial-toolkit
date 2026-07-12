# PC Stats + Claude Usage Monitor Apps Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two new circular-display apps to the M5Dial — Claude Usage (session/weekly rate-limit rings) and PC Stats (CPU/RAM/GPU gauge cluster) — fed by BLE and USB serial from two new Python scripts, porting the proven data-channel design from the sibling wiodeck project.

**Architecture:** A new shared utility (`apps/utilities/pc_link/`) owns a NimBLE GATT server and a USB-serial line reader, both feeding the same hand-rolled JSON parser (no JSON library) into two global data structs. Each app is a thin display layer: it activates the channel on open, polls it every frame, and redraws when the relevant struct's version counter changes. Two Python scripts (forked from wiodeck's) collect and send the data.

**Tech Stack:** PlatformIO / Arduino framework on ESP32-S3, NimBLE-Arduino (already a project dependency), LovyanGFX (`LGFX_Sprite` canvas), Python 3 (`httpx`, `psutil`, optional `pynvml`/`wmi`/`bleak`) for the host-side tools.

## Global Constraints

- BLE device name: `M5DIAL-001`.
- BLE service UUID: `4d354449-414c-0000-0000-000000000001`.
- BLE RX characteristic UUID: `4d354449-414c-0000-0000-000000000002`.
- Serial baud: `115200` (cosmetic for native USB CDC, kept for convention/parity with the Python scripts' `BAUD_RATE`).
- Claude usage JSON keys: `s` (session %), `sr` (session reset mins), `w` (weekly %), `wr` (weekly reset mins), `st` (status string), `srt`/`wrt` (human-readable reset strings), `ok`.
- PC stats JSON keys: `cpu`, `ct` (cpu temp, -1=N/A), `ram`, `rb` (ram string), `gpu` (-1=N/A), `gt` (gpu temp, -1=N/A), `gn` (gpu name), `ok`. No network fields (out of scope).
- Claude Usage staleness timeout: 90s. PC Stats staleness timeout: 6s.
- Claude Usage accent color: coral `0xD97757`. PC Stats gauge colors: CPU cyan, RAM green, GPU magenta (`CYBER::CYAN`/`CYBER::GREEN`/`CYBER::MAGENTA`).
- Both apps: long press = exit (700ms threshold, matching this project's existing button-hold convention). No turn/short-press interaction.
- This project has no unit test framework — verification is `pio run` (compile) plus manual on-device checks. Build command:
  `export PATH="$HOME/.platformio/penv/Scripts:$PATH"; cd "/c/dev/PlatformIO/m5stack-dial-toolkit" && pio run` (use the Bash tool, not PowerShell — PowerShell in this environment mangles this project's font build flags). Expect `========================= [SUCCESS] Took ...`.

---

### Task 1: `PC_LINK` shared data channel

**Files:**
- Create: `src/apps/utilities/pc_link/pc_link.h`
- Create: `src/apps/utilities/pc_link/pc_link.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Produces (consumed by Tasks 3 and 4):
  ```cpp
  namespace PC_LINK
  {
      struct ClaudeUsageData {
          float session_pct;
          int   session_reset_mins;
          float weekly_pct;
          int   weekly_reset_mins;
          char  status[16];
          char  session_reset_str[56];
          char  weekly_reset_str[56];
          bool  valid;
      };
      struct PcStatsData {
          int  cpu_pct;
          int  cpu_temp;
          int  ram_pct;
          char ram_str[20];
          int  gpu_pct;
          int  gpu_temp;
          char gpu_name[32];
          bool valid;
      };
      extern ClaudeUsageData claudeUsage;
      extern uint16_t        claudeUsageVersion;
      extern PcStatsData     pcStats;
      extern uint16_t        pcStatsVersion;
      void init();
      void setActive(bool active);
      void poll();
      const char* bleAddress();
  }
  ```

- [ ] **Step 1: Create `src/apps/utilities/pc_link/pc_link.h`**

```cpp
/**
 * @file pc_link.h
 * @brief Shared BLE + USB-serial data channel for the Claude Usage and PC
 *        Stats apps. Ported from the wiodeck project's claudeUsage.cpp /
 *        sysStats.cpp / bluetooth.cpp — same JSON wire schema and parsing
 *        approach, retargeted onto this project's NimBLE-Arduino stack.
 */
#pragma once
#include <stdint.h>

namespace PC_LINK
{
    struct ClaudeUsageData
    {
        float session_pct        = 0.0f;
        int   session_reset_mins = 0;
        float weekly_pct         = 0.0f;
        int   weekly_reset_mins  = 0;
        char  status[16]         = "unknown";
        char  session_reset_str[56] = "";
        char  weekly_reset_str[56]  = "";
        bool  valid = false;
    };

    struct PcStatsData
    {
        int  cpu_pct     = 0;
        int  cpu_temp    = -1;
        int  ram_pct     = 0;
        char ram_str[20] = "";
        int  gpu_pct     = -1;
        int  gpu_temp    = -1;
        char gpu_name[32] = "";
        bool valid = false;
    };

    extern ClaudeUsageData claudeUsage;
    extern uint16_t        claudeUsageVersion;
    extern PcStatsData     pcStats;
    extern uint16_t        pcStatsVersion;

    /* Call once, from main.cpp's setup() after hal.init(): starts Serial
     * and prepares (but does not advertise) the BLE GATT service. */
    void init();

    /* Start/stop BLE advertising. Call setActive(true) from an app's
     * onCreate() and setActive(false) from its onDestroy(). */
    void setActive(bool active);

    /* Call every onRunning(): drains pending serial/BLE data and updates
     * claudeUsage/pcStats + their version counters. */
    void poll();

    /* BLE MAC address string, for the no-data screen's "ADDR:" line. */
    const char* bleAddress();
}
```

- [ ] **Step 2: Create `src/apps/utilities/pc_link/pc_link.cpp`**

```cpp
/**
 * @file pc_link.cpp
 * @brief See pc_link.h.
 */
#include "pc_link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>

namespace PC_LINK
{
    static const char* TAG = "pc_link";

    #define PC_LINK_DEVICE_NAME  "M5DIAL-001"
    #define PC_LINK_SERVICE_UUID "4d354449-414c-0000-0000-000000000001"
    #define PC_LINK_RX_CHAR_UUID "4d354449-414c-0000-0000-000000000002"

    ClaudeUsageData claudeUsage;
    uint16_t        claudeUsageVersion = 0;
    PcStatsData     pcStats;
    uint16_t        pcStatsVersion = 0;

    /* ---- BLE ---- */
    static NimBLEServer* s_server        = nullptr;
    static bool          s_connected     = false;
    static bool          s_wasConnected  = false;
    static bool          s_active        = false;   // advertising desired by the open app

    static volatile bool s_blePending = false;
    static char          s_bleBuf[512];

    class RxCallbacks : public NimBLECharacteristicCallbacks
    {
        void onWrite(NimBLECharacteristic* pChar) override
        {
            NimBLEAttValue val = pChar->getValue();
            size_t len = val.length();
            if (len > 0 && len < sizeof(s_bleBuf))
            {
                memcpy(s_bleBuf, val.c_str(), len);
                s_bleBuf[len] = '\0';
                s_blePending  = true;
            }
        }
    };

    class ServerCallbacks : public NimBLEServerCallbacks
    {
        void onConnect(NimBLEServer*)    override { s_connected = true;  }
        void onDisconnect(NimBLEServer*) override { s_connected = false; }
    };

    /* ---- Parsing (hand-rolled, no JSON library — mirrors wiodeck) ---- */

    static bool parseClaudeUsageJson(const char* json)
    {
        const char* p;

        p = strstr(json, "\"s\":");
        if (!p) return false;
        claudeUsage.session_pct = (float)atoi(p + 4);

        p = strstr(json, "\"sr\":");
        if (!p) return false;
        claudeUsage.session_reset_mins = atoi(p + 5);

        p = strstr(json, "\"w\":");
        if (!p) return false;
        claudeUsage.weekly_pct = (float)atoi(p + 4);

        p = strstr(json, "\"wr\":");
        if (!p) return false;
        claudeUsage.weekly_reset_mins = atoi(p + 5);

        p = strstr(json, "\"st\":\"");
        if (!p) return false;
        p += 6;
        int i = 0;
        while (*p && *p != '"' && i < 15) claudeUsage.status[i++] = *p++;
        claudeUsage.status[i] = '\0';

        p = strstr(json, "\"srt\":\"");
        if (p) {
            p += 7;
            int i2 = 0;
            while (*p && *p != '"' && i2 < 55) claudeUsage.session_reset_str[i2++] = *p++;
            claudeUsage.session_reset_str[i2] = '\0';
        }

        p = strstr(json, "\"wrt\":\"");
        if (p) {
            p += 7;
            int i3 = 0;
            while (*p && *p != '"' && i3 < 55) claudeUsage.weekly_reset_str[i3++] = *p++;
            claudeUsage.weekly_reset_str[i3] = '\0';
        }

        claudeUsage.valid = true;
        claudeUsageVersion++;
        return true;
    }

    static bool parsePcStatsJson(const char* json)
    {
        const char* p;

        p = strstr(json, "\"cpu\":");
        if (!p) return false;
        pcStats.cpu_pct = atoi(p + 6);

        p = strstr(json, "\"ct\":");
        pcStats.cpu_temp = p ? atoi(p + 5) : -1;

        p = strstr(json, "\"ram\":");
        if (!p) return false;
        pcStats.ram_pct = atoi(p + 6);

        p = strstr(json, "\"rb\":\"");
        if (p) {
            p += 6;
            int i = 0;
            while (*p && *p != '"' && i < 19) pcStats.ram_str[i++] = *p++;
            pcStats.ram_str[i] = '\0';
        }

        p = strstr(json, "\"gpu\":");
        pcStats.gpu_pct = p ? atoi(p + 6) : -1;

        p = strstr(json, "\"gt\":");
        pcStats.gpu_temp = p ? atoi(p + 5) : -1;

        p = strstr(json, "\"gn\":\"");
        if (p) {
            p += 6;
            int i = 0;
            while (*p && *p != '"' && i < 31) pcStats.gpu_name[i++] = *p++;
            pcStats.gpu_name[i] = '\0';
        }

        pcStats.valid = true;
        pcStatsVersion++;
        return true;
    }

    static void dispatch(const char* line)
    {
        if (strstr(line, "\"cpu\":"))
            parsePcStatsJson(line);
        else
            parseClaudeUsageJson(line);
    }

    /* ---- Serial ---- */
    static char serialBuf[512];
    static int  serialPos = 0;

    static void checkSerial()
    {
        while (Serial.available())
        {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r')
            {
                if (serialPos > 0)
                {
                    serialBuf[serialPos] = '\0';
                    dispatch(serialBuf);
                    serialPos = 0;
                }
            }
            else if (serialPos < (int)sizeof(serialBuf) - 1)
            {
                serialBuf[serialPos++] = c;
            }
            else
            {
                serialPos = 0;
            }
        }
    }

    static void checkBLE()
    {
        if (s_blePending)
        {
            s_blePending = false;
            dispatch(s_bleBuf);
        }

        /* Restart advertising after a disconnect, only while a screen wants it. */
        if (!s_connected && s_wasConnected && s_active)
        {
            delay(500);
            NimBLEDevice::startAdvertising();
        }
        s_wasConnected = s_connected;
    }

    /* ---- Public API ---- */

    void init()
    {
        Serial.begin(115200);

        NimBLEDevice::init(PC_LINK_DEVICE_NAME);

        s_server = NimBLEDevice::createServer();
        s_server->setCallbacks(new ServerCallbacks());

        NimBLEService* svc = s_server->createService(PC_LINK_SERVICE_UUID);
        NimBLECharacteristic* rxChar = svc->createCharacteristic(
            PC_LINK_RX_CHAR_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        rxChar->setCallbacks(new RxCallbacks());
        svc->start();

        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        adv->addServiceUUID(PC_LINK_SERVICE_UUID);
        adv->setScanResponse(true);
        adv->setMinPreferred(0x06);
        adv->setMinPreferred(0x12);

        ESP_LOGI(TAG, "init done, device name %s", PC_LINK_DEVICE_NAME);
    }

    void setActive(bool active)
    {
        s_active = active;
        if (active)
            NimBLEDevice::startAdvertising();
        else
            NimBLEDevice::stopAdvertising();
    }

    void poll()
    {
        checkSerial();
        checkBLE();
    }

    const char* bleAddress()
    {
        static std::string addr;
        addr = NimBLEDevice::getAddress().toString();
        return addr.c_str();
    }
}
```

- [ ] **Step 3: Wire `PC_LINK::init()` into `main.cpp`**

`src/main.cpp` currently reads:

```cpp
#include <Arduino.h>
#include "hal/hal.h"
#include "apps/app.h"
#include "apps/launcher/launcher.h"

static HAL::HAL hal;

void setup()
{
    /* Hardware init */
    hal.init();
}
```

Change to:

```cpp
#include <Arduino.h>
#include "hal/hal.h"
#include "apps/app.h"
#include "apps/launcher/launcher.h"
#include "apps/utilities/pc_link/pc_link.h"

static HAL::HAL hal;

void setup()
{
    /* Hardware init */
    hal.init();

    /* PC Stats / Claude Usage data channel (BLE + USB serial) */
    PC_LINK::init();
}
```

(The `loop()` function is unchanged.)

- [ ] **Step 4: Build to verify it compiles**

Run:
```bash
export PATH="$HOME/.platformio/penv/Scripts:$PATH"; cd "/c/dev/PlatformIO/m5stack-dial-toolkit" && pio run
```
Expected: ends with `========================= [SUCCESS] Took ...`. Nothing calls `PC_LINK::setActive()`/`poll()` yet (no consuming app exists until Task 3), so a clean compile is the only checkpoint at this stage.

- [ ] **Step 5: Commit**

```bash
git add src/apps/utilities/pc_link/pc_link.h src/apps/utilities/pc_link/pc_link.cpp src/main.cpp
git commit -m "Add PC_LINK shared BLE+serial data channel for PC/Claude monitor apps"
```

---

### Task 2: Python sender scripts

**Files:**
- Create: `tools/claude_sender.py`
- Create: `tools/sysstat_sender.py`

**Interfaces:**
- Consumes: the wire schema and BLE identity from Task 1's Global Constraints (device name `M5DIAL-001`, RX characteristic UUID `4d354449-414c-0000-0000-000000000002`).
- Produces: nothing consumed by later firmware tasks — these are standalone host-side tools. Tasks 3 and 4 reference them by path/usage only.

- [ ] **Step 1: Create `tools/claude_sender.py`**

```python
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

async def _ble_find(address: str | None) -> str | None:
    if address:
        log(f"Using address: {address}")
        return address
    log(f'Scanning for "{BLE_DEVICE_NAME}"...')
    device = await BleakScanner.find_device_by_name(BLE_DEVICE_NAME, timeout=10.0)
    if device:
        log(f"Found {device.name} at {device.address}")
        return device.address
    log(f'"{BLE_DEVICE_NAME}" not found — is the M5Dial on the Claude Usage screen?')
    return None


async def _ble_run(address: str | None) -> None:
    token = ""
    while True:
        addr = await _ble_find(address)
        if not addr:
            log("Retrying scan in 10s...")
            await asyncio.sleep(10)
            continue

        try:
            async with BleakClient(addr) as client:
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
```

- [ ] **Step 2: Create `tools/sysstat_sender.py`**

```python
#!/usr/bin/env python3
"""
sysstat_sender.py — Sends PC system stats to the M5Dial over USB serial or BLE.

Reports CPU/RAM usage, temperatures, and NVIDIA GPU stats. Sends a compact
JSON line every POLL_INTERVAL seconds.

Usage:
    python sysstat_sender.py COM3           # USB serial — Windows
    python sysstat_sender.py /dev/ttyACM0   # USB serial — Linux/macOS
    python sysstat_sender.py --ble          # BLE (auto-discovers M5DIAL-001)
    python sysstat_sender.py --ble AA:BB:CC:DD:EE:FF  # BLE to specific address

Requirements:
    pip install pyserial psutil

Optional (install for the extras they unlock):
    pip install nvidia-ml-py   # NVIDIA GPU usage + temperature
    pip install wmi            # Windows CPU temperature (also needs LibreHardwareMonitor
                                # running as a service — https://github.com/LibreHardwareMonitor)
    pip install bleak          # BLE transport (required for --ble mode only)
"""

import json
import re
import subprocess
import sys
import time
import warnings
import psutil

# ---- optional: BLE via bleak (only needed for --ble mode) ----
_bleak_available = False
try:
    import asyncio
    from bleak import BleakClient, BleakScanner
    _bleak_available = True
except ImportError:
    pass

BLE_DEVICE_NAME  = "M5DIAL-001"
BLE_RX_CHAR_UUID = "4d354449-414c-0000-0000-000000000002"

POLL_INTERVAL = 2      # seconds between samples
BAUD_RATE     = 115200

warnings.filterwarnings("ignore", category=SyntaxWarning, module="wmi")
warnings.filterwarnings("ignore", category=FutureWarning, message=".*pynvml.*")

# ---- optional: NVIDIA GPU via nvidia-ml-py (import name is still pynvml) ----
_gpu_handle = None
_gpu_name   = ""
try:
    import pynvml
    pynvml.nvmlInit()
    _gpu_handle = pynvml.nvmlDeviceGetHandleByIndex(0)
    name = pynvml.nvmlDeviceGetName(_gpu_handle)
    if isinstance(name, bytes):
        name = name.decode()
    _gpu_name = name
    print(f"[gpu] NVIDIA detected: {name}")
except Exception:
    pass

if sys.platform == "darwin" and not _gpu_handle:
    try:
        out = subprocess.check_output(
            ["system_profiler", "SPDisplaysDataType"],
            text=True, stderr=subprocess.DEVNULL, timeout=8
        )
        m = re.search(r"Chipset Model:\s*(.+)", out)
        if m:
            _gpu_name = m.group(1).strip()
            print(f"[gpu] macOS GPU: {_gpu_name}")
        else:
            print("[gpu] macOS GPU detected (name unknown)")
    except Exception:
        print("[gpu] macOS GPU name unavailable")

if not _gpu_name and sys.platform != "darwin":
    print("[gpu] nvidia-ml-py not available — install with: pip install nvidia-ml-py")

# ---- optional: Windows CPU temperature via wmi + LibreHardwareMonitor --------
_wmi_lhm = None
if sys.platform == "win32" and "--ble" not in sys.argv:
    try:
        import wmi
        for ns in (r"root\LibreHardwareMonitor", r"root\OpenHardwareMonitor"):
            try:
                candidate = wmi.WMI(namespace=ns)
                list(candidate.Hardware())
                _wmi_lhm = candidate
                print(f"[cpu] Hardware monitor WMI ready ({ns.split(chr(92))[-1]})")
                break
            except Exception:
                pass
        if _wmi_lhm is None:
            print("[cpu] CPU temperature unavailable on Windows without a hardware monitor.")
            print("      Install LibreHardwareMonitor, enable WMI support, run as administrator.")
    except ImportError:
        print("[cpu] wmi package not installed — run: pip install wmi")


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def cpu_temp() -> int:
    """Return CPU package temperature in °C, or -1 if unavailable."""
    if _wmi_lhm:
        try:
            for s in _wmi_lhm.Sensor():
                if s.SensorType == "Temperature" and "CPU" in s.Name:
                    return round(float(s.Value))
        except Exception:
            pass

    try:
        temps = psutil.sensors_temperatures()
        for key in ("coretemp", "k10temp", "cpu_thermal", "acpitz", "zenpower"):
            if key in temps and temps[key]:
                return round(temps[key][0].current)
    except Exception:
        pass

    return -1


def gpu_stats() -> tuple:
    """Return (usage_pct, temp_c) or (-1, -1) if unavailable."""
    if _gpu_handle is not None:
        try:
            util = pynvml.nvmlDeviceGetUtilizationRates(_gpu_handle)
            temp = pynvml.nvmlDeviceGetTemperature(_gpu_handle, pynvml.NVML_TEMPERATURE_GPU)
            return util.gpu, temp
        except Exception:
            return -1, -1

    if sys.platform == "darwin":
        try:
            out = subprocess.check_output(
                ["ioreg", "-r", "-d", "1", "-w", "0", "-c", "IOAccelerator"],
                text=True, stderr=subprocess.DEVNULL, timeout=3
            )
            m = re.search(r'"Device Utilization %"\s*=\s*(\d+)', out)
            if not m:
                return -1, -1
            return int(m.group(1)), -1
        except Exception:
            return -1, -1

    return -1, -1


def collect() -> dict:
    cpu_pct        = round(psutil.cpu_percent(interval=None))
    ct             = cpu_temp()
    ram            = psutil.virtual_memory()
    gpu_pct, gt    = gpu_stats()

    if sys.platform == "darwin":
        try:
            out = subprocess.check_output(["vm_stat"], text=True)
            m = re.search(r"page size of (\d+)", out)
            page_size = int(m.group(1)) if m else 4096
            def _pages(key: str) -> int:
                m2 = re.search(rf"{re.escape(key)}:\s+(\d+)", out)
                return int(m2.group(1)) if m2 else 0
            ram_used = ram.total - (_pages("Pages free") + _pages("File-backed pages")) * page_size
        except Exception:
            ram_used = ram.total - ram.available
    else:
        ram_used = ram.total - ram.available

    return {
        "cpu": cpu_pct,
        "ct":  ct,
        "ram": round(ram_used / ram.total * 100),
        "rb":  f"{ram_used / 1_073_741_824:.1f}/{ram.total / 1_073_741_824:.1f}GB",
        "gpu": gpu_pct,
        "gt":  gt,
        "gn":  _gpu_name,
        "ok":  True,
    }


async def _ble_find(address: str | None) -> str | None:
    if address:
        log(f"Using address: {address}")
        return address
    log(f'Scanning for "{BLE_DEVICE_NAME}"...')
    device = await BleakScanner.find_device_by_name(BLE_DEVICE_NAME, timeout=10.0)
    if device:
        log(f"Found {device.name} at {device.address}")
        return device.address
    log(f'"{BLE_DEVICE_NAME}" not found — is the M5Dial on the PC Stats screen?')
    return None


async def _ble_run(address: str | None) -> None:
    psutil.cpu_percent(interval=None)
    while True:
        addr = await _ble_find(address)
        if not addr:
            log("Retrying in 10s...")
            await asyncio.sleep(10)
            continue
        try:
            async with BleakClient(addr) as client:
                log(f"Connected to {addr}")
                while client.is_connected:
                    data = collect()
                    line = json.dumps(data, separators=(",", ":")) + "\n"
                    await client.write_gatt_char(BLE_RX_CHAR_UUID, line.encode(), response=True)
                    log(f"Sent: {line.strip()}")
                    await asyncio.sleep(POLL_INTERVAL)
                log("Disconnected.")
        except Exception as e:
            log(f"BLE error: {e}")
        log("Reconnecting in 5s...")
        await asyncio.sleep(5)


def main() -> None:
    args = sys.argv[1:]

    if args and args[0] == "--ble":
        if not _bleak_available:
            print("BLE mode requires bleak:  pip install bleak")
            sys.exit(1)
        address = args[1] if len(args) > 1 else None
        try:
            asyncio.run(_ble_run(address))
        except KeyboardInterrupt:
            log("Stopped.")
        return

    if not args:
        print(f"Usage: python {sys.argv[0]} <port>")
        print(f"       python {sys.argv[0]} --ble [address]")
        print( "  e.g. python sysstat_sender.py COM3")
        print( "  e.g. python sysstat_sender.py --ble")
        sys.exit(1)

    import serial

    port = args[0]
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=1)
        log(f"Opened {port} at {BAUD_RATE} baud.")
    except serial.SerialException as e:
        log(f"Could not open {port}: {e}")
        sys.exit(1)

    psutil.cpu_percent(interval=None)

    try:
        while True:
            data = collect()
            line = json.dumps(data, separators=(",", ":")) + "\n"
            ser.write(line.encode())
            log(f"Sent: {line.strip()}")
            time.sleep(POLL_INTERVAL)
    except KeyboardInterrupt:
        log("Stopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Run each script standalone to verify its collection logic**

These can't be fully exercised yet (no firmware app consumes the data until
Task 3/4), but each script's own collection/auth logic can be checked in
isolation. From the repo root:

```bash
cd tools
python sysstat_sender.py COM_ANY_VALID_PORT
```
Expected: prints `[gpu] ...` / `[cpu] ...` capability lines, then repeating
`[HH:MM:SS] Sent: {"cpu":NN,...}` lines every 2s (Ctrl+C to stop) — confirms
`psutil`/optional `pynvml`/`wmi` collection works, independent of whether
anything is listening on the port.

```bash
python claude_sender.py COM_ANY_VALID_PORT
```
Expected: `Token loaded.` then `[HH:MM:SS] Sent: {"s":NN,...}` lines every
60s — confirms OAuth token loading and the Anthropic API poll work. If
`load_token()` fails, install/log into Claude Code first (see the script's
docstring) — this is expected behavior, not a bug, when no credentials
exist yet.

(Use any port the OS accepts for `serial.Serial()` — e.g. the M5Dial's own
COM port once Task 1 is flashed. `pyserial` only needs the port to open, it
does not need a listener.)

- [ ] **Step 4: Commit**

```bash
git add tools/claude_sender.py tools/sysstat_sender.py
git commit -m "Add claude_sender.py and sysstat_sender.py host tools"
```

---

### Task 3: Claude Usage app

**Files:**
- Create: `src/apps/app_claude_usage/app_claude_usage.h`
- Create: `src/apps/app_claude_usage/app_claude_usage.cpp`
- Create: `src/apps/launcher/launcher_icons/icon_claude.h`
- Modify: `src/apps/utilities/cyber_ui/cyber_ui.hpp` (add `progressRingAt`)
- Modify: `src/apps/launcher/launcher.h` (add include)
- Modify: `src/apps/launcher/launcher_render_callback.hpp` (`ICON_NUM`, icon lists)
- Modify: `src/apps/launcher/launcher.cpp` (switch case, MORE index)

**Interfaces:**
- Consumes: `PC_LINK::claudeUsage`, `PC_LINK::claudeUsageVersion`, `PC_LINK::setActive(bool)`, `PC_LINK::poll()`, `PC_LINK::bleAddress()` (Task 1). `CYBER::progressRingAt(LGFX_Sprite*, int, int, int, float, uint32_t)` (this task, defined in Step 1). `CYBER::chip(LGFX_Sprite*, const char*, uint32_t)`, `CYBER::blend(uint32_t, uint32_t, float)`, `CYBER::bigTime(LGFX_Sprite*, const char*, uint32_t, float)`, `CYBER::BG`/`CYAN`/`GREEN`/`RED`/`WHITE`/`DIMTEXT` (existing, `cyber_ui.hpp`).
- Produces: `MOONCAKE::USER_APP::ClaudeUsage` class (consumed by `launcher.cpp`'s switch in this task).

- [ ] **Step 1: Add `progressRingAt` to `cyber_ui.hpp`**

In `src/apps/utilities/cyber_ui/cyber_ui.hpp`, immediately after the existing
`progressRing()` function (search for its closing brace, just before the
`/* Title text near the top, inside the ring. */` comment), add:

```cpp
    /* Generalized version of progressRing() — arbitrary center/radius, so
     * callers can draw multiple gauges of different sizes on one canvas
     * (e.g. the Claude Usage app's concentric rings, or PC Stats' clustered
     * small gauges). Pip count auto-scales with radius (~7px between pips)
     * so smaller rings don't look sparse. progressRing() itself is left
     * untouched — it has a different fixed pip count (60) that Timer/
     * Stopwatch/Pomodoro's existing look depends on. */
    inline void progressRingAt(LGFX_Sprite* c, int cx, int cy, int radius, float p, uint32_t col)
    {
        if (p < 0) p = 0;
        if (p > 1) p = 1;

        int N = (int)(2.0f * 3.14159265f * radius / 7.0f);
        if (N < 16) N = 16;

        for (int i = 0; i < N; i++)
        {
            float theta = (-90.0f + 360.0f * i / N) * 0.01745329f;
            int x = cx + (int)(radius * cosf(theta));
            int y = cy + (int)(radius * sinf(theta));

            float d = p * N - i;
            if (d > 0.0f)
            {
                if (d < 1.0f)   c->fillSmoothCircle(x, y, 2, WHITE);
                else            c->fillCircle(x, y, 2, blend(col, BG, 0.9f));
            }
            else
            {
                c->fillCircle(x, y, 1, blend(BORDER, BG, 0.8f));
            }
        }

        c->drawCircle(cx, cy, radius + 4, blend(col, BG, 0.5f));
    }

```

- [ ] **Step 2: Create `src/apps/app_claude_usage/app_claude_usage.h`**

```cpp
/**
 * @file app_claude_usage.h
 * @brief Claude Usage — concentric dual-ring display of session (5h) and
 *        weekly (7d) rate-limit utilization, fed by PC_LINK over BLE/USB
 *        serial. Ported from the wiodeck project's claudeUsage.cpp.
 *
 *  Controls
 *   - Long press : exit to menu
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"

namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace CLAUDE_USAGE
        {
            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                uint16_t drawn_version = 0;
                bool     drawn_valid   = false;
                uint32_t last_data_ms  = 0;
            };
        }

        class ClaudeUsage : public APP_BASE
        {
            private:
                const char* _tag = "ClaudeUsage";

                void _render();

            public:
                CLAUDE_USAGE::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
```

- [ ] **Step 3: Create `src/apps/app_claude_usage/app_claude_usage.cpp`**

```cpp
/**
 * @file app_claude_usage.cpp
 * @brief See app_claude_usage.h for the concept and controls.
 */
#include "app_claude_usage.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include "../utilities/pc_link/pc_link.h"
#include <string.h>


using namespace MOONCAKE::USER_APP;

static const uint32_t CLAUDE_ACCENT = 0xD97757;   // coral — Claude's own identity, distinct from default cyan

/* Blocking button read: 0 = none, 1 = short press, 2 = long press (>700ms).
 * Same idiom as app_watchface/app_stopwatch. */
static int _read_button(HAL::HAL* hal)
{
    if (hal->encoder.btn.read())
        return 0;
    uint32_t t0 = millis();
    while (!hal->encoder.btn.read())
        delay(5);
    return (millis() - t0 > 700) ? 2 : 1;
}

static uint32_t _status_color(const char* status)
{
    if (strncmp(status, "limited", 7) == 0 || strncmp(status, "rejected", 8) == 0)
        return CYBER::RED;
    return CYBER::GREEN;
}

void ClaudeUsage::_render()
{
    LGFX_Sprite* c = _data.hal->canvas;
    c->fillScreen(CYBER::BG);

    auto& u = PC_LINK::claudeUsage;

    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);

    if (!u.valid)
    {
        c->setTextSize(2);
        c->setTextColor(CLAUDE_ACCENT);
        c->drawString("CLAUDE USAGE", 120, 70);

        c->setTextSize(1);
        c->setTextColor(CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.85f));
        c->drawString("AWAITING DATA...", 120, 100);
        c->drawString("BLE: M5DIAL-001", 120, 120);
        c->drawString("USB: claude_sender.py <port>", 120, 136);

        char addr[48];
        snprintf(addr, sizeof(addr), "ADDR: %s", PC_LINK::bleAddress());
        c->drawString(addr, 120, 156);

        _canvas_update();
        return;
    }

    CYBER::progressRingAt(c, 120, 120, 112, u.session_pct / 100.0f, CLAUDE_ACCENT);
    CYBER::progressRingAt(c, 120, 120, 85,  u.weekly_pct  / 100.0f,
                          CYBER::blend(CLAUDE_ACCENT, CYBER::WHITE, 0.3f));

    char big[4];
    snprintf(big, sizeof(big), "%2d", (int)u.session_pct);
    CYBER::bigTime(c, big, CYBER::WHITE, 0.0f);

    c->setTextSize(1);
    c->setTextColor(CLAUDE_ACCENT);
    c->drawString("SESSION 5h", 120, 145);

    char weeklyLine[24];
    snprintf(weeklyLine, sizeof(weeklyLine), "WEEKLY 7d  %d%%", (int)u.weekly_pct);
    c->setTextColor(CYBER::blend(CLAUDE_ACCENT, CYBER::WHITE, 0.3f));
    c->drawString(weeklyLine, 120, 160);

    bool limited  = (strncmp(u.status, "limited",  7) == 0);
    bool rejected = (strncmp(u.status, "rejected", 8) == 0);
    const char* statusText = limited ? "LIMITED" : (rejected ? "REJECTED" : "ALLOWED");
    CYBER::chip(c, statusText, _status_color(u.status));

    c->setTextColor(CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.85f));
    c->drawString(u.session_reset_str[0] ? u.session_reset_str : "", 120, 200);

    _canvas_update();
}

void ClaudeUsage::onSetup()
{
    setAppName("ClaudeUsage");
    setAllowBgRunning(false);

    CLAUDE_USAGE::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}

void ClaudeUsage::onCreate()
{
    _log("onCreate");

    /* Reset stale data from a previous session. */
    PC_LINK::claudeUsage.valid = false;
    PC_LINK::claudeUsageVersion++;

    PC_LINK::setActive(true);

    _data.drawn_version = PC_LINK::claudeUsageVersion;
    _data.drawn_valid   = false;
    _data.last_data_ms  = 0;

    _render();
}

void ClaudeUsage::onRunning()
{
    PC_LINK::poll();

    if (PC_LINK::claudeUsage.valid && PC_LINK::claudeUsageVersion != _data.drawn_version)
        _data.last_data_ms = millis();

    /* Staleness: revert to the no-data panel if the stream stops for >90s
     * (claude_sender.py polls every 60s, so 90s gives a 1.5x buffer). */
    if (PC_LINK::claudeUsage.valid && _data.last_data_ms &&
        millis() - _data.last_data_ms > 90000)
    {
        PC_LINK::claudeUsage.valid = false;
        PC_LINK::claudeUsageVersion++;
    }

    if (PC_LINK::claudeUsageVersion != _data.drawn_version)
    {
        _data.drawn_version = PC_LINK::claudeUsageVersion;
        _data.drawn_valid   = PC_LINK::claudeUsage.valid;
        _render();
    }

    if (_read_button(_data.hal) == 2)
    {
        destroyApp();
        return;
    }

    delay(20);
}

void ClaudeUsage::onDestroy()
{
    _log("onDestroy");
    PC_LINK::setActive(false);
}
```

- [ ] **Step 4: Generate the Claude Usage launcher icon**

Run this script (from the repo root, `python3` with no extra dependencies)
and capture its output:

```python
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
```

Save it as `tools/gen_launcher_icons.py`, run
`python tools/gen_launcher_icons.py > /tmp/claude_icon.txt`, then create
`src/apps/launcher/launcher_icons/icon_claude.h`:

```cpp
#pragma once
#include <stdint.h>

<paste the array printed by the script here>
```

(Keep `tools/gen_launcher_icons.py` in the repo — Task 4 extends it with a
second icon rather than replacing it.)

- [ ] **Step 5: Wire the new icon into `launcher_icons.h`**

In `src/apps/launcher/launcher_icons/launcher_icons.h`, add after the
existing includes:

```cpp
#include "icon_claude.h"
```

- [ ] **Step 6: Add the include to `launcher.h`**

In `src/apps/launcher/launcher.h`, add after
`#include "../app_watchface/app_watchface.h"`:

```cpp
#include "../app_claude_usage/app_claude_usage.h"
```

- [ ] **Step 7: Register the icon in `launcher_render_callback.hpp`**

In `src/apps/launcher/launcher_render_callback.hpp`:

Change `ICON_NUM` from:
```cpp
#define ICON_NUM                    7
```
to:
```cpp
#define ICON_NUM                    8
```

Change `icon_color_list` from:
```cpp
static std::array<uint32_t, ICON_NUM> icon_color_list = {
    0x00F0FF,  // Watch Face - electric cyan (home / default landing app)
    0xFF2A6D,  // BLE Volume - hot pink-magenta
    0xFCEE0A,  // Timer      - signature yellow
    0x00FF9F,  // Stopwatch  - neon mint
    0xFF003C,  // Pomodoro   - alert red
    0xFF9E00,  // Brightness - warm amber
    0x3A6B8C   // More       - steel blue
};
```
to:
```cpp
static std::array<uint32_t, ICON_NUM> icon_color_list = {
    0x00F0FF,  // Watch Face  - electric cyan (home / default landing app)
    0xFF2A6D,  // BLE Volume  - hot pink-magenta
    0xFCEE0A,  // Timer       - signature yellow
    0x00FF9F,  // Stopwatch   - neon mint
    0xFF003C,  // Pomodoro    - alert red
    0xFF9E00,  // Brightness  - warm amber
    0xD97757,  // Claude Usage - coral
    0x3A6B8C   // More        - steel blue
};
```

Change `icon_tag_list` from:
```cpp
static std::array<std::string, ICON_NUM * 2> icon_tag_list = {
    "WATCH", "",
    "VOL", "CTRL",
    "TIMER", "",
    "STOPWATCH", "",
    "POMODORO", "",
    "BRIGHTNESS", "SET",
    "MORE", ""
};
```
to:
```cpp
static std::array<std::string, ICON_NUM * 2> icon_tag_list = {
    "WATCH", "",
    "VOL", "CTRL",
    "TIMER", "",
    "STOPWATCH", "",
    "POMODORO", "",
    "BRIGHTNESS", "SET",
    "CLAUDE", "USAGE",
    "MORE", ""
};
```

Change `icon_pic_list` from:
```cpp
static std::array<const uint16_t*, ICON_NUM> icon_pic_list = {
    image_data_icon_rtc,        // Watch Face (clock icon)
    image_data_icon_volume,
    image_data_icon_timer,
    image_data_icon_stopwatch,
    image_data_icon_pomodoro,
    image_data_icon_brigntness,
    image_data_icon_more
};
```
to:
```cpp
static std::array<const uint16_t*, ICON_NUM> icon_pic_list = {
    image_data_icon_rtc,        // Watch Face (clock icon)
    image_data_icon_volume,
    image_data_icon_timer,
    image_data_icon_stopwatch,
    image_data_icon_pomodoro,
    image_data_icon_brigntness,
    image_data_icon_claude,
    image_data_icon_more
};
```

- [ ] **Step 8: Add the switch case and update the MORE index in `launcher.cpp`**

In `src/apps/launcher/launcher.cpp`, change:
```cpp
    if (selectedNum != 6)
        theme_color = icon_list[selectedNum].color;
```
to:
```cpp
    if (selectedNum != 7)
        theme_color = icon_list[selectedNum].color;
```

Change:
```cpp
        case 5:
            app_ptr = new MOONCAKE::USER_APP::Set_Brightness;
            break;
        case 6:
            app_ptr = new MOONCAKE::USER_APP::MoreMenu;
            break;
        default:
            break;
    };
```
to:
```cpp
        case 5:
            app_ptr = new MOONCAKE::USER_APP::Set_Brightness;
            break;
        case 6:
            app_ptr = new MOONCAKE::USER_APP::ClaudeUsage;
            break;
        case 7:
            app_ptr = new MOONCAKE::USER_APP::MoreMenu;
            break;
        default:
            break;
    };
```

- [ ] **Step 9: Build to verify it compiles**

Run:
```bash
export PATH="$HOME/.platformio/penv/Scripts:$PATH"; cd "/c/dev/PlatformIO/m5stack-dial-toolkit" && pio run
```
Expected: ends with `========================= [SUCCESS] Took ...`.

- [ ] **Step 10: Manual on-device verification**

Flash (`pio run --target upload`) and check:
- The launcher now shows 8 icons; the new coral asterisk icon opens Claude Usage.
- On open, it shows the no-data panel with `BLE: M5DIAL-001` / `USB: claude_sender.py <port>` / a real `ADDR:` line.
- Run `python tools/claude_sender.py <the M5Dial's COM port>` — the app populates with the concentric rings, a session percentage in the center, and an ALLOWED/LIMITED/REJECTED chip.
- Stop the script, close and reopen the app on the M5Dial, then run `python tools/claude_sender.py --ble` — same result over BLE.
- Stop the sender entirely and wait 90s — the app reverts to the no-data panel.
- Long-press the button — the app exits back to the launcher.

- [ ] **Step 11: Commit**

```bash
git add src/apps/app_claude_usage src/apps/utilities/cyber_ui/cyber_ui.hpp \
        src/apps/launcher/launcher_icons/icon_claude.h \
        src/apps/launcher/launcher_icons/launcher_icons.h \
        src/apps/launcher/launcher.h src/apps/launcher/launcher_render_callback.hpp \
        src/apps/launcher/launcher.cpp tools/gen_launcher_icons.py
git commit -m "Add Claude Usage app: concentric dual-ring session/weekly display"
```

---

### Task 4: PC Stats app

**Files:**
- Create: `src/apps/app_pc_stats/app_pc_stats.h`
- Create: `src/apps/app_pc_stats/app_pc_stats.cpp`
- Create: `src/apps/launcher/launcher_icons/icon_pcstats.h`
- Modify: `tools/gen_launcher_icons.py` (add the PC Stats icon)
- Modify: `src/apps/launcher/launcher.h` (add include)
- Modify: `src/apps/launcher/launcher_icons/launcher_icons.h` (add include)
- Modify: `src/apps/launcher/launcher_render_callback.hpp` (`ICON_NUM`, icon lists)
- Modify: `src/apps/launcher/launcher.cpp` (switch case, MORE index)

**Interfaces:**
- Consumes: `PC_LINK::pcStats`, `PC_LINK::pcStatsVersion`, `PC_LINK::setActive(bool)`, `PC_LINK::poll()`, `PC_LINK::bleAddress()` (Task 1). `CYBER::progressRingAt` (Task 3, `cyber_ui.hpp`). `CYBER::CYAN`/`GREEN`/`MAGENTA`/`AMBER`/`RED`/`DIMTEXT`/`BG`/`blend()` (existing, `cyber_ui.hpp`).
- Produces: `MOONCAKE::USER_APP::PcStats` class (consumed by `launcher.cpp`'s switch in this task).

- [ ] **Step 1: Create `src/apps/app_pc_stats/app_pc_stats.h`**

```cpp
/**
 * @file app_pc_stats.h
 * @brief PC Stats — a triangular cluster of three independent ring gauges
 *        (CPU / RAM / GPU), fed by PC_LINK over BLE/USB serial. Ported
 *        from the wiodeck project's sysStats.cpp.
 *
 *  Controls
 *   - Long press : exit to menu
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"

namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace PC_STATS
        {
            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                uint16_t drawn_version = 0;
                bool     drawn_valid   = false;
                uint32_t last_data_ms  = 0;
            };
        }

        class PcStats : public APP_BASE
        {
            private:
                const char* _tag = "PcStats";

                void _render();

            public:
                PC_STATS::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
```

- [ ] **Step 2: Create `src/apps/app_pc_stats/app_pc_stats.cpp`**

```cpp
/**
 * @file app_pc_stats.cpp
 * @brief See app_pc_stats.h for the concept and controls.
 */
#include "app_pc_stats.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include "../utilities/pc_link/pc_link.h"


using namespace MOONCAKE::USER_APP;

/* Blocking button read: 0 = none, 1 = short press, 2 = long press (>700ms). */
static int _read_button(HAL::HAL* hal)
{
    if (hal->encoder.btn.read())
        return 0;
    uint32_t t0 = millis();
    while (!hal->encoder.btn.read())
        delay(5);
    return (millis() - t0 > 700) ? 2 : 1;
}

/* One gauge: ring + big percentage + label + (temp or subinfo) sub-line.
 * pct < 0 shows "N/A" instead of a percentage (e.g. no NVIDIA GPU found).
 * temp < 0 falls back to showing subinfo instead (e.g. RAM's "8.2/16GB"). */
static void _draw_gauge(LGFX_Sprite* c, int cx, int cy, const char* label,
                        int pct, uint32_t col, int temp, const char* subinfo)
{
    float frac = (pct < 0) ? 0.0f : pct / 100.0f;
    CYBER::progressRingAt(c, cx, cy, 44, frac, col);

    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);

    c->setTextSize(2);
    if (pct >= 0)
    {
        char v[8]; snprintf(v, sizeof(v), "%d%%", pct);
        c->setTextColor(col);
        c->drawString(v, cx, cy - 10);
    }
    else
    {
        c->setTextColor(CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.7f));
        c->drawString("N/A", cx, cy - 10);
    }

    c->setTextSize(1);
    c->setTextColor(CYBER::blend(col, CYBER::BG, 0.4f));
    c->drawString(label, cx, cy + 8);

    if (temp >= 0)
    {
        char t[8]; snprintf(t, sizeof(t), "%dC", temp);
        uint32_t tcol = (temp < 60) ? CYBER::GREEN : ((temp < 80) ? CYBER::AMBER : CYBER::RED);
        c->setTextColor(tcol);
        c->drawString(t, cx, cy + 20);
    }
    else if (subinfo && subinfo[0])
    {
        c->setTextColor(CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.7f));
        c->drawString(subinfo, cx, cy + 20);
    }
}

void PcStats::_render()
{
    LGFX_Sprite* c = _data.hal->canvas;
    c->fillScreen(CYBER::BG);

    auto& s = PC_LINK::pcStats;

    c->setFont(&fonts::Font0);
    c->setTextSize(1);
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(CYBER::CYAN);
    c->drawString("PC STATS", 120, 20);

    if (!s.valid)
    {
        c->setTextColor(CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.85f));
        c->drawString("AWAITING DATA...", 120, 100);
        c->drawString("BLE: M5DIAL-001", 120, 120);
        c->drawString("USB: sysstat_sender.py <port>", 120, 136);

        char addr[48];
        snprintf(addr, sizeof(addr), "ADDR: %s", PC_LINK::bleAddress());
        c->drawString(addr, 120, 156);

        _canvas_update();
        return;
    }

    _draw_gauge(c, 72,  96,  "CPU", s.cpu_pct, CYBER::CYAN,    s.cpu_temp, nullptr);
    _draw_gauge(c, 168, 96,  "RAM", s.ram_pct, CYBER::GREEN,   -1, s.ram_str);
    _draw_gauge(c, 120, 188, "GPU", s.gpu_pct, CYBER::MAGENTA, s.gpu_temp, s.gpu_name);

    _canvas_update();
}

void PcStats::onSetup()
{
    setAppName("PcStats");
    setAllowBgRunning(false);

    PC_STATS::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}

void PcStats::onCreate()
{
    _log("onCreate");

    PC_LINK::pcStats.valid = false;
    PC_LINK::pcStatsVersion++;

    PC_LINK::setActive(true);

    _data.drawn_version = PC_LINK::pcStatsVersion;
    _data.drawn_valid   = false;
    _data.last_data_ms  = 0;

    _render();
}

void PcStats::onRunning()
{
    PC_LINK::poll();

    if (PC_LINK::pcStats.valid && PC_LINK::pcStatsVersion != _data.drawn_version)
        _data.last_data_ms = millis();

    /* Staleness: revert to the no-data panel if the stream stops for >6s
     * (sysstat_sender.py polls every 2s, so 6s = 3 missed packets). */
    if (PC_LINK::pcStats.valid && _data.last_data_ms &&
        millis() - _data.last_data_ms > 6000)
    {
        PC_LINK::pcStats.valid = false;
        PC_LINK::pcStatsVersion++;
    }

    if (PC_LINK::pcStatsVersion != _data.drawn_version)
    {
        _data.drawn_version = PC_LINK::pcStatsVersion;
        _data.drawn_valid   = PC_LINK::pcStats.valid;
        _render();
    }

    if (_read_button(_data.hal) == 2)
    {
        destroyApp();
        return;
    }

    delay(20);
}

void PcStats::onDestroy()
{
    _log("onDestroy");
    PC_LINK::setActive(false);
}
```

- [ ] **Step 3: Extend `tools/gen_launcher_icons.py` with the PC Stats icon**

Append to the end of `tools/gen_launcher_icons.py` (created in Task 3):

```python

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
```

Run `python tools/gen_launcher_icons.py > /tmp/both_icons.txt` — the file
now contains both arrays (`image_data_icon_claude`, printed first, then
`image_data_icon_pcstats`). Create
`src/apps/launcher/launcher_icons/icon_pcstats.h`:

```cpp
#pragma once
#include <stdint.h>

<paste the image_data_icon_pcstats array printed by the script here>
```

- [ ] **Step 4: Wire the new icon into `launcher_icons.h`**

In `src/apps/launcher/launcher_icons/launcher_icons.h`, add after
`#include "icon_claude.h"`:

```cpp
#include "icon_pcstats.h"
```

- [ ] **Step 5: Add the include to `launcher.h`**

In `src/apps/launcher/launcher.h`, add after
`#include "../app_claude_usage/app_claude_usage.h"`:

```cpp
#include "../app_pc_stats/app_pc_stats.h"
```

- [ ] **Step 6: Register the icon in `launcher_render_callback.hpp`**

Change `ICON_NUM` from `8` to:
```cpp
#define ICON_NUM                    9
```

Change `icon_color_list` from (Task 3's version):
```cpp
static std::array<uint32_t, ICON_NUM> icon_color_list = {
    0x00F0FF,  // Watch Face  - electric cyan (home / default landing app)
    0xFF2A6D,  // BLE Volume  - hot pink-magenta
    0xFCEE0A,  // Timer       - signature yellow
    0x00FF9F,  // Stopwatch   - neon mint
    0xFF003C,  // Pomodoro    - alert red
    0xFF9E00,  // Brightness  - warm amber
    0xD97757,  // Claude Usage - coral
    0x3A6B8C   // More        - steel blue
};
```
to:
```cpp
static std::array<uint32_t, ICON_NUM> icon_color_list = {
    0x00F0FF,  // Watch Face   - electric cyan (home / default landing app)
    0xFF2A6D,  // BLE Volume   - hot pink-magenta
    0xFCEE0A,  // Timer        - signature yellow
    0x00FF9F,  // Stopwatch    - neon mint
    0xFF003C,  // Pomodoro     - alert red
    0xFF9E00,  // Brightness   - warm amber
    0xD97757,  // Claude Usage - coral
    0x00F0FF,  // PC Stats     - electric cyan
    0x3A6B8C   // More         - steel blue
};
```

Change `icon_tag_list` from (Task 3's version) to add a PC Stats row before
MORE:
```cpp
static std::array<std::string, ICON_NUM * 2> icon_tag_list = {
    "WATCH", "",
    "VOL", "CTRL",
    "TIMER", "",
    "STOPWATCH", "",
    "POMODORO", "",
    "BRIGHTNESS", "SET",
    "CLAUDE", "USAGE",
    "PC", "STATS",
    "MORE", ""
};
```

Change `icon_pic_list` from (Task 3's version) to add the new icon before
`image_data_icon_more`:
```cpp
static std::array<const uint16_t*, ICON_NUM> icon_pic_list = {
    image_data_icon_rtc,        // Watch Face (clock icon)
    image_data_icon_volume,
    image_data_icon_timer,
    image_data_icon_stopwatch,
    image_data_icon_pomodoro,
    image_data_icon_brigntness,
    image_data_icon_claude,
    image_data_icon_pcstats,
    image_data_icon_more
};
```

- [ ] **Step 7: Add the switch case and update the MORE index in `launcher.cpp`**

Change (from Task 3's version):
```cpp
    if (selectedNum != 7)
        theme_color = icon_list[selectedNum].color;
```
to:
```cpp
    if (selectedNum != 8)
        theme_color = icon_list[selectedNum].color;
```

Change (from Task 3's version):
```cpp
        case 6:
            app_ptr = new MOONCAKE::USER_APP::ClaudeUsage;
            break;
        case 7:
            app_ptr = new MOONCAKE::USER_APP::MoreMenu;
            break;
        default:
            break;
    };
```
to:
```cpp
        case 6:
            app_ptr = new MOONCAKE::USER_APP::ClaudeUsage;
            break;
        case 7:
            app_ptr = new MOONCAKE::USER_APP::PcStats;
            break;
        case 8:
            app_ptr = new MOONCAKE::USER_APP::MoreMenu;
            break;
        default:
            break;
    };
```

- [ ] **Step 8: Build to verify it compiles**

Run:
```bash
export PATH="$HOME/.platformio/penv/Scripts:$PATH"; cd "/c/dev/PlatformIO/m5stack-dial-toolkit" && pio run
```
Expected: ends with `========================= [SUCCESS] Took ...`.

- [ ] **Step 9: Manual on-device verification**

Flash (`pio run --target upload`) and check:
- The launcher now shows all 9 icons at the wider 288°-spread arc; the new
  gauge-glyph icon opens PC Stats.
- On open, it shows the no-data panel with `BLE: M5DIAL-001` /
  `USB: sysstat_sender.py <port>` / a real `ADDR:` line.
- Run `python tools/sysstat_sender.py <the M5Dial's COM port>` — the app
  populates with three independent ring gauges (CPU/RAM/GPU), each with its
  own color, percentage, and label; RAM shows its GB string, CPU/GPU show
  temperatures when available (or nothing if unavailable).
- Stop the script, close and reopen the app, then run
  `python tools/sysstat_sender.py --ble` — same result over BLE.
- Stop the sender and wait 6s — the app reverts to the no-data panel.
- Long-press the button — the app exits back to the launcher.
- Re-verify Claude Usage (Task 3) still works correctly now that the arc
  has 9 icons instead of 8.

- [ ] **Step 10: Commit**

```bash
git add src/apps/app_pc_stats src/apps/launcher/launcher_icons/icon_pcstats.h \
        src/apps/launcher/launcher_icons/launcher_icons.h \
        src/apps/launcher/launcher.h src/apps/launcher/launcher_render_callback.hpp \
        src/apps/launcher/launcher.cpp tools/gen_launcher_icons.py
git commit -m "Add PC Stats app: CPU/RAM/GPU triangular gauge cluster"
```
