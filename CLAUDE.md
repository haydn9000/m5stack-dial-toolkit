# CLAUDE.md — M5Stack Dial Toolkit

Context for AI assistants working on this project.

## Project Overview

A PlatformIO + Arduino port of the official [M5Dial-UserDemo](https://github.com/m5stack/M5Dial-UserDemo) (originally ESP-IDF) for the **M5Stack Dial v1.1** device. The M5Dial is an ESP32-S3 based rotary encoder device with a 240×240 round GC9A01 display, FT3267 touchpad, PCF8563 RTC, buzzer, and a built-in WS1850S RFID reader.

## Build Commands

```bash
# Build
pio run

# Build + Upload
pio run --target upload

# Monitor serial output
pio device monitor
```

## Hardware: M5Stack Dial v1.1

| Component | Details |
|---|---|
| MCU | ESP32-S3 |
| Display | GC9A01 240×240 round SPI LCD (pins: MOSI=5, SCLK=6, DC=4, CS=7, RST=8, BL=9) |
| Touchpad | FT3267 I2C (SDA=11, SCL=12, INT=14) |
| Encoder A/B | GPIO 41, 40 |
| Button (encoder press) | GPIO 42 |
| Buzzer | GPIO 3 |
| Power hold | GPIO 46 |
| Grove I2C | SDA=13, SCL=15 |
| RTC | PCF8563 on I2C_NUM_0 |
| RFID | WS1850S (MFRC522-compatible) on I2C_NUM_0 |

Pin definitions are in `src/hal/hal_common_define.h`.

## Source Structure

```
src/
├── main.cpp                        # Arduino setup()/loop() entry point
├── hal/                            # Hardware abstraction layer
│   ├── hal.h / hal.cpp             # Main HAL class
│   ├── hal_common_define.h         # Pin definitions
│   ├── display/hal_display.hpp     # LovyanGFX LGFX_StampRing class (GC9A01)
│   ├── buzzer/hal_buzzer.hpp       # Buzzer (wraps Arduino tone())
│   ├── tp/hal_tp.hpp               # FT3267 touchpad driver
│   ├── rtc/hal_rtc.hpp             # PCF8563 RTC driver
│   └── utils/
│       ├── ESP32Encoder/           # Encoder with wasMoved()/getDirection() API
│       └── Button/                 # Debounced button with callback support
└── apps/                           # MOONCAKE app framework
    ├── app.h                       # APP_BASE lifecycle base class
    ├── common_define.h             # Shared macros (_log, _canvas_update, etc.)
    ├── launcher/                   # Main launcher with circular icon menu
    ├── app_lcd_test/               # LCD test app
    ├── app_rtc_test/               # RTC clock display app
    ├── app_rfid_test/              # RFID card scanner app
    ├── app_set_brightness/         # Brightness control app
    ├── app_wifi_scan/              # WiFi network scanner app
    ├── app_ble_server/             # BLE heart rate server app
    ├── app_ble_volume/             # BLE HID volume controller app (encoder=vol, button=mute; while muted, down lowers level/drives host to 0, up unmutes; first power-on starts at 0/muted)
    ├── app_temp_demo/              # Temperature/animation demo app
    ├── app_more_menu/              # Scrollable "more" menu (power off, etc.)
    ├── app_template/               # Blank template for new apps
    └── utilities/
        ├── smooth_menu/            # Animated menu library (Simple_Menu)
        ├── gui_base/               # GUI_Base class with common drawing helpers
        ├── ble_demo/               # BLE demo interface + Arduino BLE implementation
        └── wifi_common_test/       # WiFi scan utility (esp_wifi API)
```

## App Framework (MOONCAKE)

All apps inherit from `MOONCAKE::APP_BASE` and live in the `MOONCAKE::USER_APP` namespace. Override lifecycle methods:

```cpp
void onSetup();    // Called once at install; set name, init _data from getUserData()
void onCreate();   // Called when app opens
void onRunning();  // Called every frame — poll encoder, update display here
void onDestroy();  // Called when app closes; free resources
```

Close an app from within `onRunning()` by calling `destroyApp()`.

## Key Conventions

- **Canvas pattern**: All drawing goes to `hal->canvas` (an `LGFX_Sprite`), then flushed with `hal->canvas->pushSprite(0, 0)` — or the `_canvas_update()` macro from `common_define.h`.
- **Encoder**: Configured HALF-QUAD via `attachHalfQuad(41, 40)`, which yields **2 raw counts per physical detent**. The simple API (`hal->encoder.wasMoved(true)` + `hal->encoder.getDirection()`) is fine for menus, but for precise step control read raw counts with `hal->encoder.getCount()` directly and act only on whole detents (delta of ±2), consuming the delta in steps of 2. Reading `getCount()/2` truncates and causes phantom reverse ticks from contact bounce — avoid it. Increasing raw count = clockwise = "up". See `/memories/repo/m5dial-encoder.md`.
- **Per-notch beep**: HAL installs a global `encoder._move_callback` that beeps on every detent. Apps that manage their own sounds (e.g. volume) suppress it by saving & nulling `_move_callback`/`_user_data` in `onCreate()` and restoring them in `onDestroy()`.
- **Button**: `hal->encoder.btn.read()` returns `false` (LOW) when pressed. Typical pattern: `if (!hal->encoder.btn.read()) { while (!hal->encoder.btn.read()) delay(5); destroyApp(); }`
- **Buzzer**: `hal->buzz.tone(frequency_hz, duration_ms)` — short beeps on encoder movement are wired up in HAL callbacks.
- **Persistence (NVS)**: Use the Arduino `Preferences` library, namespace `"settings"`. Brightness is stored under key `"bright"` (restored during the boot splash in `hal.cpp`). Persist in `onDestroy()`.
- **Logging**: Use `_log(fmt, ...)`, `_log_w(...)`, `_log_e(...)` macros (wrap `ESP_LOGI/W/E`). Tag is `_tag` (a `const char*` member set per class).
- **Fonts**: `GUI_FONT_CN_BIG` = `&fonts::efontCN_24`, `GUI_FONT_CN_SMALL` = `&fonts::efontCN_16_b` (defined as build flags). The 7-segment readout uses `&fonts::Font7`.
- **`delay()`/`millis()`**: Provided by the Arduino framework — never redefine them.

## Porting Notes (ESP-IDF → Arduino)

The original project used ESP-IDF 5.1.3. Key differences in this port:

| Original | This port |
|---|---|
| `app_main()` | Arduino `setup()` / `loop()` |
| `vTaskDelay` / `esp_timer_get_time` | Arduino `delay()` / `millis()` |
| NimBLE raw C API (`nimble/nimble_port.h`) | Arduino BLE (Bluedroid) — `src/apps/utilities/ble_demo/ble_demo_arduino.cpp` |
| `hal/arduino/` (custom Arduino compat layer) | Excluded — real Arduino framework used |
| `hal/file_system/` (wear-levelling NVS) | Excluded — not needed |
| `hal/lvgl/` (LVGL porting layer) | Excluded — LVGL disabled (`LVGL_ENABLE 0`) |
| `fillSmoothRoundRectInDifference()` (custom patched LovyanGFX) | No-op removed — original was fully commented out anyway |
| `SDL_Delay(ms)` macros in GUI files | Removed — Arduino `delay()` used |

## Files Excluded from Build (`build_src_filter`)

- `hal/arduino/` — custom ESP-IDF Arduino compat headers
- `hal/file_system/` — wear-levelling filesystem
- `hal/lvgl/` — LVGL porting layer
- `apps/app_factory_test/` — factory test app (not compiled; enable with `-DENABLE_FACTORY_TEST`)
- `apps/utilities/adc_read/` — ADC utility (factory test only)
- `apps/utilities/rgb/` — RGB LED utility (factory test only)
- `apps/utilities/wifi_common_test/wifi_factory_test.c` — WiFi factory test
- `apps/utilities/ble_demo/ble_demo.c` — raw NimBLE C implementation (replaced)
- `apps/utilities/ble_demo/gatt_svr.c` — raw NimBLE GATT server (replaced)

## Dependencies

- **LovyanGFX** (`lovyan03/LovyanGFX ^1.2.0`) — display driver
- **ESP32 BLE Keyboard** (`t-vk/ESP32 BLE Keyboard ^0.3.2`) — BLE HID media keys for the volume app (built against NimBLE via `-DUSE_NIMBLE`)
- **NimBLE-Arduino** (`^1.4.3`) — BLE stack backing the volume app's HID keyboard
- **Preferences** — Arduino NVS wrapper used for persisting settings (brightness)
- **RC522** (`lib/rc522/`) — local copy of esp-idf-rc522, drives the M5Dial's built-in WS1850S (MFRC522-compatible) for the RFID app
- Arduino ESP32 framework BLE library — used by the BLE server app

## Adding a New App

1. Copy `src/apps/app_template/` to a new directory.
2. Add the app's `#include` to `src/apps/launcher/launcher.h`.
3. Add a `case N: app_ptr = new MOONCAKE::USER_APP::YourApp; break;` in `launcher.cpp::_app_open_callback()`.
4. Add an icon entry in `launcher.h` (icon list, color, tags).

## Launcher Layout

The **main launcher** shows 3 icons clustered in a tight arc at the top (not spread around the full ring): **VOL** (BLE Volume, index 0), **BRIGHTNESS** (Set Brightness, index 1), and **MORE** (index 2). `ICON_NUM` in `launcher_render_callback.hpp` controls the icon/menu count; the arc spacing/center are set by `arc_step`/`arc_center` in `launcher.cpp` (`_menu_init` and `_icon_list_init`). The switch in `launcher.cpp::_app_open_callback()` must match the icon order.

All other apps live in the **More menu** (`app_more_menu/`), shown as a vertical list with each app's icon drawn to the left of its name. To move/add an app there:
- Add the app's `#include` to `app_more_menu.h`.
- Add its tag to `tag_list` in `app_more_menu.cpp::_create_menu()` (text x-offset is 56 to leave room for icons).
- Add the matching icon (or `nullptr`) at the same index in the `setIcons({...})` list in `_create_menu()`.
- Add a matching `if (selected_item_tag == "...") { _run_app(new ...App, themeColor, image_data_icon_...); return; }` in `more_menu_selected_callback.cpp`.

`MoreMenu::_run_app()` runs a sub-app's full lifecycle (with optional GUI init using a shared 42×42 `_app_icon` sprite) until it quits, then deletes it. The render callback (`more_menu_render_callback.hpp`) draws the per-row icons at half scale via `setIconCanvas()`/`setIcons()`. The launcher and More menu both include `launcher_icons/launcher_icons.h` for icon image data (the arrays are `static const`, so no linker conflict).
