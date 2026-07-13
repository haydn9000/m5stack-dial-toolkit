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


async def _ble_find(address: str | None):
    if address:
        log(f"Using address: {address}")
        return address
    log(f'Scanning for "{BLE_DEVICE_NAME}"...')
    device = await BleakScanner.find_device_by_name(BLE_DEVICE_NAME, timeout=10.0)
    if device:
        log(f"Found {device.name} at {device.address}")
        return device
    log(f'"{BLE_DEVICE_NAME}" not found — is the M5Dial on the PC Stats screen?')
    return None


async def _ble_run(address: str | None) -> None:
    psutil.cpu_percent(interval=None)
    while True:
        target = await _ble_find(address)
        if not target:
            log("Retrying in 10s...")
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
