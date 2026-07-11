# Power Off: real cutoff on battery, deep sleep when plugged in

## Context

The M5Dial's RTC (PCF8563) is powered from `VBAT_IN`, the same rail as the board's
LiPo battery input — independent of the ESP32's own power state. As long as
external DC/USB power (or an installed backup LiPo) stays connected, the clock
keeps correct time regardless of what the firmware does.

Per M5Stack's own docs, the board's `HOLD` pin (GPIO46) only actually cuts power
when there's no external USB/DC supply. If USB/DC is connected, pulling `HOLD`
low has no effect — the system keeps running.

The More Menu's existing "Power Off" item (`more_menu_selected_callback.cpp`)
calls `hal->powerOff()` and then unconditionally does `delay(4000);
hal->powerOn();` — always re-asserting `HOLD` a few seconds later regardless of
whether power was actually cut. This is why Power Off appears to "turn itself
back on after a few seconds": it always does, by design of that leftover code
(most likely added so the feature wouldn't brick a demo unit sitting on USB
power at a trade show).

## Goal

Make "Power Off" do the right thing in both power scenarios, without needing to
explicitly detect which one is active:

- **Battery-powered (no USB/DC):** `HOLD=0` genuinely cuts all power. Nothing
  needs to change here except removing the auto re-power-on. Wake is already
  handled at the hardware level (WAKE button / reconnecting power) — no code
  involved.
- **Plugged into USB/DC:** `HOLD=0` does nothing, so the firmware keeps running
  past that call. That fact alone (code execution continuing) is the signal
  that we're on external power — no separate VBUS-sense pin exists on this
  board to check directly. In this case, fall into a real ESP32 deep sleep to
  minimize draw, and wake on a touchscreen tap.

## Why deep sleep, and why tap-to-wake

Deep sleep was chosen over light sleep: the watchface (the app users leave
running) has no meaningful state to preserve across a sleep, so the lowest
power option wins. Deep sleep is a full reboot on wake, re-entering the normal
boot flow (splash → launcher → Watchface), which is fine since the RTC is
unaffected either way.

Deep-sleep wake sources on the ESP32-S3 are restricted to GPIO0-21 (`ext0`/
`ext1`), confirmed against Espressif's sleep-mode docs. The board's WAKE/encoder
button is on GPIO42 — outside that range — so a button press cannot wake the
chip from true deep sleep; this is a SoC hardware limit, not something
software can work around. The touchscreen interrupt line (`TP_INT`, GPIO14) is
in range and already configured by the touch driver (`hal_tp.hpp`) as a
pulled-up input, active-low on touch. It's the closest available equivalent to
a button press, and needs no hardware changes, so tap-to-wake was chosen over
dropping deep sleep entirely or using a timer-only wake (which wouldn't
support waking on demand).

RTC-alarm wake was considered and ruled out: the PCF8563's interrupt pin isn't
wired to any ESP32 GPIO on this board (confirmed against the schematic and
`hal_common_define.h`), so it isn't a usable wake source here.

## Design

### `HAL::powerOffOrSleep()` (new method, `hal.cpp`/`hal.h`)

Encapsulates the full "try real off, otherwise sleep" sequence, alongside the
existing `powerOn()`/`powerOff()`:

1. Call `powerOff()` (`HOLD=0`), as today.
2. A short settle delay (~200ms) — if a battery is installed and no external
   power is present, execution never reaches past this point; the board is
   already fully powered down.
3. If execution continues (external power present): configure
   `esp_sleep_enable_ext0_wakeup(GPIO_NUM_14, 0)` (wake when the touch
   interrupt pin goes low) and call `esp_deep_sleep_start()`. This call does
   not return — the chip resets on wake and re-runs `setup()`/`loop()` from
   scratch.

### `more_menu_selected_callback.cpp` — "Power Off" handler

Unchanged: blank the screen, `rtc.clearIRQ()` / `rtc.disableIRQ()` (already
inert since RTC IRQ isn't wired to anything, but harmless and left as-is to
keep this change minimal), then the existing 500ms delay.

Changed: replace the `hal->powerOff(); delay(4000); hal->powerOn();` sequence
with a single call to `hal->powerOffOrSleep()`.

## Out of scope

- Adding a backup LiPo battery (separate hardware step, already discussed
  elsewhere) — this feature doesn't change what's needed for RTC retention
  across a *true* full power loss (no USB and no battery); it only fixes the
  "plugged in" case and the auto-repower bug.
- RTC-alarm wake (not wired on this board).
- Light sleep / state-preserving resume (deep sleep only, full reboot on
  wake).
- Any change to the WAKE button's existing behavior in other apps.

## Testing / verification

This is hardware-dependent behavior that can't be exercised from a dev
machine. Verification is manual, on-device, covering both paths:

- **Unplugged, running on an installed battery:** select Power Off → device
  stays off (no self-wake) → press the WAKE button → device powers back on
  with the correct time still showing.
- **Plugged into USB/DC:** select Power Off → screen blanks and stays blank
  (no self-wake after a few seconds) → tap the screen → device reboots into
  the watchface with the correct time still showing.
