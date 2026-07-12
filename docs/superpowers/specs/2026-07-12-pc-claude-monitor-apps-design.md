# PC Stats + Claude Usage monitor apps

## Context

The sibling [wiodeck](../../../../../wiodeck) project (a Seeed Wio Terminal toolkit) already
solved this exact problem: a "Sys Stats" screen and a "Claude Usage" screen,
each fed by a Python script over USB serial or BLE, with a compact
hand-parsed JSON wire format (no JSON library dependency). This design
ports that proven approach — BLE/serial transport, wire schema, and the
data-collection logic in the Python scripts — onto the M5Dial's circular
240×240 display, with new circular-native UI for both apps and using this
project's own BLE stack (NimBLE) rather than wiodeck's.

Both apps are independent displays over one shared transport, so this is
one cohesive spec rather than two: the transport/schema is common
infrastructure, and each app is a thin display layer on top of it.

## Goals

- A **Claude Usage** app showing Claude Code's session (5h) and weekly (7d)
  rate-limit utilization, matching wiodeck's `claude_sender.py` metrics
  exactly.
- A **PC Stats** app showing CPU, RAM, and GPU usage (temperatures as a
  best-effort extra), matching a trimmed subset of wiodeck's
  `sysstat_sender.py` metrics (network bandwidth dropped — out of scope,
  see below).
- Both apps live as new icons on the main launcher arc (not the More Menu).
- Data arrives over BLE and USB serial simultaneously, matching wiodeck's
  dual-mode parity.

## Out of scope

- Network bandwidth (wiodeck's `nd`/`nu` fields) — dropped per the PC
  stats scope decision; only CPU/RAM/GPU (+temps) are shown.
- A "Process Watch" (top-5 CPU processes) equivalent — not requested, not
  built.
- Any change to wiodeck itself — this is a one-way port of its approach,
  not a shared codebase.
- RTC-alarm or timer-based data refresh scheduling on the M5Dial side —
  the device is purely a passive receiver; the Python scripts own polling
  cadence.

## Architecture

### Shared data channel: `apps/utilities/pc_link/`

A new shared utility, structurally parallel to `apps/utilities/ble_demo/`,
providing both transports to both apps:

**BLE (NimBLE, this project's existing BLE stack):**
- Device name: `M5DIAL-001`
- Service UUID: `4d354449-414c-0000-0000-000000000001`
- RX characteristic UUID: `4d354449-414c-0000-0000-000000000002` — write
  and write-without-response, host writes one full JSON line per packet.
  Packets can exceed the default 23-byte ATT MTU (PC stats/Claude usage
  JSON runs ~150-250 bytes); NimBLE's GATT server handles the standard
  "Write Long Characteristic Values" procedure automatically for
  `write=True` writes exceeding the negotiated MTU, the same way wiodeck's
  BLE stack already does for its larger payloads — no extra chunking logic
  needed on either side.
- Advertising starts only while a consuming app (`Claude Usage` or
  `PC Stats`) is open, and stops on exit — matches wiodeck's
  `bleSetActive()` lifecycle and this project's existing BLE app pattern
  (`BLE_Volume`'s `onCreate`/`onDestroy`).

**Serial (ESP32-S3 native USB CDC):**
- `Serial.begin(115200)` added to `HAL::init()` (not currently called
  anywhere in this project). Baud rate is cosmetic for native USB CDC but
  matches wiodeck's convention and the Python scripts' `BAUD_RATE`
  constant.
- A newline-terminated line reader (`checkSerial()`), directly ported from
  wiodeck's `claudeUsage.cpp` implementation: buffers characters until
  `\n`/`\r`, then dispatches the completed line.

**Shared dispatch and state**, both directly ported from wiodeck's
`claudeUsage.cpp`/`sysStats.cpp`/`bluetooth.cpp`:
- Content-sniffed dispatch: a line containing `"cpu":` is PC stats,
  otherwise Claude usage (only two data types now that Process Watch is
  out of scope, so no third branch is needed).
- Two global structs with a version counter each (`ClaudeUsageData` +
  `claudeDataVersion`, `PcStatsData` + `pcStatsDataVersion`), exactly
  mirroring wiodeck's `UsageData`/`dataVersion` and
  `SysStatsData`/`sysDataVersion`. Apps poll the version counter in
  `onRunning()` and redraw only when it changes.
- Hand-rolled `strstr`/`atoi`/`strtof` parsing (no JSON library) — proven
  by wiodeck, keeps this dependency-free.

**Public interface** (`pc_link.h`):
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
        int  cpu_temp;     // -1 = unavailable
        int  ram_pct;
        char ram_str[20];  // "8.2/16GB"
        int  gpu_pct;      // -1 = unavailable
        int  gpu_temp;     // -1 = unavailable
        char gpu_name[32]; // empty if unavailable
        bool valid;
    };

    extern ClaudeUsageData claudeUsage;
    extern uint16_t        claudeUsageVersion;
    extern PcStatsData     pcStats;
    extern uint16_t        pcStatsVersion;

    void init();                 // call once from HAL::init(): Serial.begin() + BLE service setup (not advertising)
    void setActive(bool active); // start/stop BLE advertising; call on app onCreate(true)/onDestroy(false)
    void poll();                 // call every onRunning(): checkSerial() + checkBLE() equivalent
    const char* bleAddress();    // for the no-data screen's "ADDR: ..." line
}
```

### Wire schema

**Claude usage** (identical to wiodeck's `claude_sender.py` output):
```json
{"s":45,"sr":142,"w":67,"wr":2580,"st":"allowed","srt":"Resets 3:50pm (EDT)","wrt":"Resets May 18, 8pm (EDT)","ok":true}
```
`s`/`w` = session/weekly percentage (int 0-100). `sr`/`wr` = minutes until
reset. `st` = `"allowed"` / `"limited"` / `"rejected"`. `srt`/`wrt` =
human-readable reset time strings.

**PC stats** (wiodeck's `sysstat_sender.py` schema minus `nd`/`nu`):
```json
{"cpu":42,"ct":58,"ram":68,"rb":"8.2/16GB","gpu":15,"gt":61,"gn":"NVIDIA GeForce RTX 4070","ok":true}
```
`cpu`/`ram`/`gpu` = percentage (int, `-1` = unavailable for gpu). `ct`/`gt`
= temperature °C (`-1` = unavailable). `rb` = human-readable RAM string.
`gn` = GPU name (empty string if unavailable).

### Python tooling: `tools/`

Two scripts forked from wiodeck's, same structure and optional-dependency
handling, retargeted at the new BLE identity:

- `tools/claude_sender.py` — same Anthropic API polling, OAuth token
  loading (`~/.claude/.credentials.json` / macOS Keychain), 60s poll
  interval. `BLE_DEVICE_NAME = "M5DIAL-001"`,
  `BLE_RX_CHAR_UUID = "4d354449-414c-0000-0000-000000000002"`.
- `tools/sysstat_sender.py` — same `psutil`/`pynvml`/`wmi` collection
  logic, 2s poll interval, same BLE identity constants. Network fields
  (`nd`/`nu`) removed from the `collect()` payload.

## Claude Usage app (`apps/app_claude_usage/`)

Concentric dual-ring gauge on the 240×240 canvas, reusing this project's
existing drawing conventions (`CYBER::blend()`, `Font7` big-number
treatment, `cyber_ui::chip()`):

- Outer ring (radius ~112, matching the watchface's rim radius): session
  (5h) percentage, segmented arc sweep.
- Inner ring (radius ~85): weekly (7d) percentage, same sweep style.
- Center: big Font7 percentage for **session** (the tighter/more urgent
  window) in the app's accent color, with the weekly percentage shown
  smaller just below it.
- Status chip (top, reusing `cyber_ui::chip()`): `ALLOWED` / `LIMITED` /
  `REJECTED`, colored per wiodeck's convention (green-ish/amber for
  allowed, red for limited/rejected).
- Bottom text: whichever reset string is more imminent (session's, since
  it recurs every 5h).
- Accent color: coral `0xD97757` (matching wiodeck's Claude palette),
  distinct from the default cyan used elsewhere in this project — same
  "give Claude its own identity" choice wiodeck made.
- No-data state: connection instructions (`BLE: M5DIAL-001` /
  `USB: claude_sender.py <port>`), matching wiodeck's terminal-style
  "NO_DATA" panel adapted to the circular canvas.
- Staleness timeout: 90s (matches wiodeck's — `claude_sender.py` polls
  every 60s).

Controls: long press = exit (matching every other app's convention). No
turn/short-press interaction needed since both rings are always visible.

## PC Stats app (`apps/app_pc_stats/`)

Three independent small ring gauges in a triangular cluster, each its own
full circle (not nested):

- CPU: center (72, 96), radius 52, cyan accent.
- RAM: center (168, 96), radius 52, green accent.
- GPU: center (120, 188), radius 52, magenta accent.
- Each gauge: segmented arc sweep (same drawing technique as the
  watchface's rim ticks, scaled down), big percentage number inside, small
  label below it, and — when available (`cpu_temp`/`gpu_temp` != -1) — a
  compact temperature sub-line under the number. RAM shows its `ram_str`
  ("8.2/16GB") as its sub-line instead of a temperature. GPU shows `N/A` in
  a dim color when `gpu_pct == -1` (no NVIDIA GPU detected).
- Small title text near the top (`PC STATS`), no full `hudChrome` (that
  chrome's header/frame would eat into the vertical space the three-gauge
  cluster needs).
- No-data / staleness handling identical in structure to Claude Usage:
  connection-instructions panel, 6s staleness timeout (matches wiodeck's
  — `sysstat_sender.py` polls every 2s, so 6s = 3 missed packets).

Controls: long press = exit. No turn interaction (all three gauges are
always visible simultaneously, per your choice).

## Launcher integration

- `ICON_NUM` (in `launcher_render_callback.hpp`) goes from 7 to 9.
- `arc_step` stays 36° (unchanged) — the arc widens from 216° to 288° of
  spread, per your choice to keep spacing rather than compress it.
- Two new 42×42 `uint16_t` RGB565 icons, in the same flat single-color
  style as the existing icon set (`launcher_icons/`):
  - Claude Usage: a six-point asterisk (matching wiodeck's Claude-brand
    motif, `drawClaudeStar` in `claudeUsage.cpp`).
  - PC Stats: a simple gauge/chip glyph.
- New `case` entries in `launcher.cpp::_app_open_callback()`, matching the
  existing switch pattern. Icon order/index shift means the existing MORE
  special-case (`selectedNum != 6`) needs its index updated to match
  MORE's new position in a 9-icon arc.

## Error handling

Both apps follow wiodeck's exact model: a `valid` flag on each data
struct, a version counter that increments on every successful parse, and
a staleness timeout that flips `valid` back to `false` (reverting to the
no-data panel) if the stream goes quiet. Malformed lines (missing a
required key) are silently dropped, matching wiodeck's `parseUsageJson`/
`parseSysStatsJson` behavior of returning `false` without touching the
existing data — a bad packet never corrupts a previously-good reading.

## Testing

This project has no unit test framework; verification is `pio run`
(compile) plus manual on-device checks, matching this project's existing
convention (see the RTC/deep-sleep work). Specific manual checks:

- Both apps show their no-data panel with correct connection instructions
  before any data arrives.
- `tools/claude_sender.py <port>` and `tools/sysstat_sender.py <port>`
  (serial mode) each drive their app to a populated, correct-looking
  display.
- `tools/claude_sender.py --ble` and `tools/sysstat_sender.py --ble` each
  do the same over BLE, including reconnect after the M5Dial app is closed
  and reopened.
- Staleness: closing the sender script mid-session reverts the app to the
  no-data panel after the appropriate timeout (90s Claude / 6s PC stats).
- Launcher: all 9 icons are reachable and visually distinct at the new
  36°-step, 288°-spread arc.
