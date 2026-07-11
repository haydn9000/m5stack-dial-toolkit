# Power Off / Deep Sleep Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the More Menu's "Power Off" so it stays off instead of auto-waking after a few seconds, and add a deep-sleep fallback (tap-to-wake) for when the device is left on external power.

**Architecture:** A new `HAL::powerOffOrSleep()` method (alongside the existing `HAL::powerOn()`/`HAL::powerOff()` in `hal.cpp`/`hal.h`) tries a real hardware power-off first (`HOLD=0` via `powerOff()`); if execution continues past that (proof external power is present, since `HOLD` has no effect on this board while plugged into USB/DC), it configures an ESP32-S3 deep-sleep wake source on the touchscreen interrupt pin and calls `esp_deep_sleep_start()`. The More Menu's "Power Off" handler is updated to call this one method instead of its current `powerOff(); delay(4000); powerOn();` sequence.

**Tech Stack:** PlatformIO / Arduino framework on ESP32-S3 (Espressif32 platform), no unit test framework in this repo — verification is `pio run` (compile) plus manual on-device checks, per this project's existing conventions.

## Global Constraints

- Deep-sleep wake source: GPIO14 (`TP_INT`, the touchscreen interrupt pin), wake level 0 (active-low) — per `docs/superpowers/specs/2026-07-11-power-off-deep-sleep-design.md`.
- Settle delay after `powerOff()` before falling into deep sleep: ~200ms.
- New method name: `HAL::powerOffOrSleep()` — must match exactly, since it's the only interface point between the two tasks below.
- No RTC-alarm wake, no light sleep, no timer-only wake — deep sleep + tap-to-wake only (see spec's "Out of scope").
- Build command for verification: run from the repo root —
  `export PATH="$HOME/.platformio/penv/Scripts:$PATH"; cd "/c/dev/PlatformIO/m5stack-dial-toolkit" && pio run`
  Expected on success: output ends with `========================= [SUCCESS] Took ...`

---

### Task 1: Add `HAL::powerOffOrSleep()`

**Files:**
- Modify: `src/hal/hal.h` (add method declaration)
- Modify: `src/hal/hal.cpp` (add `#include <esp_sleep.h>` and the implementation)

**Interfaces:**
- Consumes: `HAL::HAL::powerOff()` (existing, `src/hal/hal.cpp` — sets `HOLD` GPIO low), `delay()` (Arduino, existing).
- Produces: `void HAL::HAL::powerOffOrSleep()` — a public method on the `HAL::HAL` class, callable as `hal->powerOffOrSleep()`. Never returns if it reaches deep sleep; returns normally only if the caller isn't actually on a `HAL::HAL` pointer (i.e., in practice this call is the last thing that happens on that code path).

- [ ] **Step 1: Add the method declaration to `hal.h`**

In `src/hal/hal.h`, the `HAL` class currently declares (around line 67-68):

```cpp
        void powerOn();
        void powerOff();
```

Change to:

```cpp
        void powerOn();
        void powerOff();
        void powerOffOrSleep();
```

- [ ] **Step 2: Add the include and implementation to `hal.cpp`**

In `src/hal/hal.cpp`, the includes at the top currently read:

```cpp
#include "hal.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <string.h>
#include <cmath>
#include <cstdio>
```

Add `<esp_sleep.h>` to this block:

```cpp
#include "hal.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <string.h>
#include <cmath>
#include <cstdio>
```

Then, immediately after the existing `HAL::powerOff()` implementation (find it by searching for `void HAL::powerOff()` — it ends with the `gpio_set_level(pin_pwr_holding, 0);` line and closing brace), add:

```cpp
    void HAL::powerOffOrSleep()
    {
        powerOff();

        /* If we're still executing, HOLD had no effect — external power
         * (USB/DC) is present, since this board only honors HOLD=0 when
         * running off battery alone. Give the (no-op) cutoff a moment to
         * settle, then fall into deep sleep instead; the RTC keeps running
         * off VBAT_IN either way. */
        delay(200);

        esp_sleep_enable_ext0_wakeup(GPIO_NUM_14, 0);   // wake when TP_INT goes low (touch)
        esp_deep_sleep_start();
    }
```

- [ ] **Step 3: Build to verify it compiles**

Run:
```bash
export PATH="$HOME/.platformio/penv/Scripts:$PATH"; cd "/c/dev/PlatformIO/m5stack-dial-toolkit" && pio run
```
Expected: ends with `========================= [SUCCESS] Took ...` (this method isn't called from anywhere yet, so a clean compile is the only checkpoint at this stage).

- [ ] **Step 4: Commit**

```bash
git add src/hal/hal.h src/hal/hal.cpp
git commit -m "$(cat <<'EOF'
Add HAL::powerOffOrSleep() for real power-off with deep-sleep fallback

Tries the existing HOLD-based power-off first; if the board is on
external power (where HOLD has no effect), falls into ESP32 deep
sleep instead, waking on a touchscreen tap.
EOF
)"
```

---

### Task 2: Wire the More Menu's "Power Off" into `powerOffOrSleep()`

**Files:**
- Modify: `src/apps/app_more_menu/more_menu_selected_callback.cpp:117-133`

**Interfaces:**
- Consumes: `HAL::HAL::powerOffOrSleep()` (Task 1, exact name/signature above).

- [ ] **Step 1: Replace the "Power Off" handler's power sequence**

In `src/apps/app_more_menu/more_menu_selected_callback.cpp`, the handler currently reads:

```cpp
    if (selected_item_tag == "Power Off")
    {
        _data.hal->canvas->fillScreen(TFT_BLACK);
        _canvas_update();


        _data.hal->rtc.clearIRQ();
        _data.hal->rtc.disableIRQ();

        delay(500);

        _data.hal->powerOff();

        
        delay(4000);
        _data.hal->powerOn();
    }
```

Replace the final block (`_data.hal->powerOff(); delay(4000); _data.hal->powerOn();`) so the handler reads:

```cpp
    if (selected_item_tag == "Power Off")
    {
        _data.hal->canvas->fillScreen(TFT_BLACK);
        _canvas_update();


        _data.hal->rtc.clearIRQ();
        _data.hal->rtc.disableIRQ();

        delay(500);

        _data.hal->powerOffOrSleep();
    }
```

- [ ] **Step 2: Build to verify it compiles**

Run:
```bash
export PATH="$HOME/.platformio/penv/Scripts:$PATH"; cd "/c/dev/PlatformIO/m5stack-dial-toolkit" && pio run
```
Expected: ends with `========================= [SUCCESS] Took ...`

- [ ] **Step 3: Manual on-device verification (both power scenarios)**

This behavior is hardware-dependent and can't be exercised on a dev machine — flash the built firmware (`pio run --target upload`) and check both paths:

  - **Unplugged, running on an installed battery:** open More Menu → Power Off. Expected: device stays off (no self-wake after a few seconds, unlike before this change). Press the WAKE button. Expected: device powers back on, boots to the watchface, and shows the correct time (RTC was never powered down).
  - **Plugged into USB/DC:** open More Menu → Power Off. Expected: screen blanks and stays blank (no self-wake). Tap the screen. Expected: device reboots (splash screen plays) into the watchface, showing the correct time.

  If either check fails, do not proceed — return to Task 1/2 and re-diagnose before committing further work on top.

- [ ] **Step 4: Commit**

```bash
git add src/apps/app_more_menu/more_menu_selected_callback.cpp
git commit -m "$(cat <<'EOF'
Fix Power Off auto-waking after a few seconds

The More Menu's Power Off handler always re-asserted HOLD 4 seconds
after cutting it, so the device could never actually stay off. It now
calls HAL::powerOffOrSleep(), which stays off on battery power and
falls into a real deep sleep (tap-to-wake) when running on external
power, where HOLD has no effect.
EOF
)"
```
