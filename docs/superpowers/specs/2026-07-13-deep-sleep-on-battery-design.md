# Sleep: always deep sleep, on battery or external power alike

> Supersedes `2026-07-11-power-off-deep-sleep-design.md`. That doc's battery
> path — a `HOLD=0` hard cutoff, recoverable via the physical WAKE button —
> was implemented and tested on a real unit with a newly added LiPo. It
> didn't hold up; see below.

## Context

The prior design made `HAL::powerOffOrSleep()` branch on power source: a real
`HOLD=0` cutoff on battery alone (for near-zero draw), falling back to ESP32
deep sleep only when external power kept the board running past the cutoff
attempt (since `HOLD=0` is a no-op with USB/DC connected).

That was implemented and manually tested after wiring a 3.7V LiPo into the
Dial's battery socket. The battery path did cut power as expected. But
waking it back up did not work as designed: pressing the physical
WAKE/encoder button (GPIO42) with the board fully cut over on battery alone
did nothing. Only reconnecting USB power brought it back. This makes sense
in hindsight — once `HOLD=0` cuts the board's rail, the ESP32 itself has no
power at all, so there's no firmware left running to read GPIO42, and this
board has no confirmed separate hardware path from the button straight into
the power-hold circuit (unlike some other M5Stack boards with a PMIC wired
that way). The original doc's assumption that "wake is already handled at
the hardware level" was untested at the time and turned out to be wrong.

## Goal

Make "Sleep" (renamed from "Power Off" — it no longer ever fully powers the
board off) behave identically regardless of power source, and always be
recoverable by the user without needing a USB cable on hand:

- Never pull `HOLD` (GPIO46) low from this code path. `powerOn()` already
  latches it high once at boot (`hal.cpp::init()`) and it simply stays that
  way — on battery alone, that's what keeps the board powered through the
  sleep.
- Always configure `esp_sleep_enable_ext0_wakeup(GPIO_NUM_14, 0)` (wake on a
  touchscreen tap) and call `esp_deep_sleep_start()`, whether running on
  USB/DC or battery.

This trades away the near-zero-draw "true off" state on battery (previously
theoretically achievable, practically unrecoverable) for a state that's
always recoverable by tapping the screen — no cable required.

## What changed

- `HAL::powerOffOrSleep()` renamed to `HAL::deepSleep()` (`hal.h`/`hal.cpp`)
  — the name no longer needs to describe two branches, since there's only
  one now. The `powerOff()` call and the settle `delay(200)` that preceded
  it were removed; the function now goes straight to backlight-off → wake
  source config → `esp_deep_sleep_start()`.
- `powerOff()` itself (raw `HOLD=0`) is untouched and still exists — it's
  still called directly by the factory-test RTC test
  (`apps/app_factory_test/test/rtc_test.cpp`, not compiled into normal
  builds) — but nothing in the normal app flow calls it anymore.
- More Menu: the item and its tag were renamed from `"Power Off"` to
  `"Sleep"` (`app_more_menu.cpp`, `more_menu_selected_callback.cpp`) to match
  what it actually does now.

## Bug found in the first cut: GPIO46's level doesn't survive deep sleep

Implementing the goal above as "just never call `powerOff()`" wasn't enough.
On-device testing found that on battery alone, tapping the screen after
"Sleep" still didn't wake the device — only reconnecting USB did, the exact
symptom this doc set out to fix.

Root cause: GPIO46 is outside the ESP32-S3's RTC domain (GPIO0-21 only, per
the WAKE-button note above). A plain digital GPIO's driven level is not
preserved across deep sleep — confirmed directly in this project's bundled
`driver/gpio.h`: *"The state of the digital gpio cannot be held during
Deep-sleep, and it will resume to hold at its default pin state when the
chip wakes up from Deep-sleep. If the digital gpio also needs to be held
during Deep-sleep, `gpio_deep_sleep_hold_en` should also be called."*
`deepSleep()` never called that, so the moment `esp_deep_sleep_start()` ran,
GPIO46 stopped being driven high — on battery, that instantly collapsed the
power path (the same effect as the old hard cutoff, just unintentional this
time); on USB it was invisible, since external power keeps the rail up
regardless of GPIO46.

Fix: `deepSleep()` now calls `gpio_hold_en(GPIO_NUM_46)` +
`gpio_deep_sleep_hold_en()` before sleeping, latching the pin's high level
through the whole sleep. `powerOn()` calls `gpio_hold_dis(GPIO_NUM_46)` as
its first step on the next boot, since a hold — per the same header —
persists across the reset that follows a deep-sleep wake and would
otherwise silently block later `gpio_set_level()` calls (including any
future `powerOff()`) from taking effect.

## Open questions / not done here

- **Actual sleep-current draw on battery hasn't been measured.** M5Stack's
  own published deep-sleep figures for the Dial (1.9–6µA) likely assume the
  touch controller (FT3267) and display driver (GC9A01) are also put into a
  low-power state; this implementation only cuts backlight PWM and puts the
  ESP32-S3 itself into deep sleep. The FT3267 has to stay active enough to
  drive `TP_INT` low on a tap, so it can't be fully powered down regardless.
  Real draw is more likely in the tens-of-µA-to-low-mA range than M5Stack's
  quoted figure. Measuring with a multimeter/USB power meter, and looking
  into explicit low-power/sleep-in commands for the FT3267 and GC9A01, is a
  follow-up, not done as part of this change.
- RTC-alarm wake, light sleep, and the WAKE-button hardware limit are
  unchanged from the prior doc — see that doc for that reasoning, still
  valid.

## Addendum: Power Off reinstated as a separate, explicit item

After the GPIO46 hold fix above confirmed Sleep works correctly on battery,
a genuine hard-cutoff "Power Off" was requested back as an *additional*
More Menu item — not a replacement for Sleep — for users who want the
lowest possible draw and are fine with USB being the only way to revive the
device afterward. `HAL::powerOffOrSleep()` was reintroduced for this: tries
`powerOff()` (`HOLD=0`) first, and only if execution continues past that
(external power present) falls back to `deepSleep()`. The
battery-alone-is-unrecoverable-except-via-USB limitation from the original
2026-07-11 doc is real and unchanged — it's just now an opt-in choice
instead of the only behavior, with Sleep available as the always-recoverable
default.

## Testing / verification

Manual, on-device, with a LiPo installed in the battery socket:

- **Running on battery alone:** select Sleep → screen blanks, device stays
  dark (no self-wake) → tap the touchscreen → device reboots into the
  watchface with the correct time still showing.
- **Plugged into USB/DC:** same as above — select Sleep → screen blanks →
  tap the touchscreen → reboots into the watchface with correct time.
- Confirmed the failure mode being fixed: with the old `HOLD=0` battery path,
  selecting Power Off and then pressing the WAKE button did **not** bring
  the device back — only reconnecting USB did.

## Addendum: renamed "Sleep" → "Deep Sleep"

The More Menu item (and its tag in `app_more_menu.cpp` /
`more_menu_selected_callback.cpp`) was renamed from `"Sleep"` to
`"Deep Sleep"` after the Power Off item above was reinstated, so the two
options read unambiguously side by side and the label makes clear this is a
full-reboot-on-wake sleep, not a state-preserving one. `HAL::deepSleep()`
itself was already named this; only the menu-facing string changed. All
"Sleep" references earlier in this doc reflect the name at the time each
section was written — see the actual `app_more_menu.cpp` for the current
label.
