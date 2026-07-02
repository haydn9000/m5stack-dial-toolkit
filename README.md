# M5Stack Dial Toolkit

A **PlatformIO + Arduino** firmware for the [M5Stack Dial](https://docs.m5stack.com/en/core/M5Dial) (v1.1) — a port of the official [M5Dial-UserDemo](https://github.com/m5stack/M5Dial-UserDemo) (originally ESP-IDF) rebuilt on the Arduino framework, with a redesigned cyberpunk UI and a BLE volume controller.

The M5Dial is an ESP32-S3 rotary-encoder device with a 240×240 round display, touchscreen, RTC, buzzer, and a built-in RFID reader.

<p align="center">
  <img src="img/m5stack-dial.jpg" alt="M5Stack Dial" width="360">
</p>

## Features

- **Watch Face** — the home screen: a cyberpunk "SCOPE" radar clock. A second-sweep hand rotates the rim once a minute with a phosphor trail, `HH:MM` sits in the centre, and turning the dial flips to a date layer (weekday, date, month progress). Press returns to the time, hold exits.
- **BLE Volume Controller** — acts as a Bluetooth HID device so the dial controls your computer's master volume. Rotate to adjust, press to mute. Mute-aware: while muted you can keep lowering the level (and drive the host all the way to 0) or rotate up to unmute.
- **Stopwatch / Timer / Pomodoro** — three time apps. The stopwatch times with centisecond precision, the countdown timer ends with a loud resonant alarm, and the pomodoro tracks focus/break cycles with session dots.
- **Set Time** — set the RTC on-device: turn to adjust the highlighted field, press for the next field, hold to save.
- **Brightness Control** — adjust screen brightness as a clean 0–100 % (persisted to NVS, restored on boot).
- **More menu** — extra apps: LCD test, RTC clock, Set Time, RFID scanner, WiFi scanner, BLE heart-rate server, temperature demo, and power off.
- **Neon-cyberpunk UI** — an electric-yellow / cyan / red palette on near-black, CRT scanlines, an asymmetric HUD frame, 7-segment readouts with a glitchy chromatic-aberration shadow, a "decrypt" boot-in animation on the time apps, and cyberpunk buzzer sound effects (sweeps and arpeggios rather than flat beeps). HTML design prototypes live in [`design/`](design/).

## Screenshots

| Home Menu | Volume Controller | Brightness |
|:---:|:---:|:---:|
| <img src="img/home-menu.jpg" alt="Home menu" width="240"> | <img src="img/gui-ble-volume.jpg" alt="BLE volume controller HUD" width="240"> | <img src="img/gui-brightness.jpg" alt="Brightness HUD" width="240"> |

| More Menu | Timer Screen | |
|:---:|:---:|:---:|
| <img src="img/more-menu.jpg" alt="More menu with app icons" width="240"> | <img src="img/timer-screen.jpg" alt="Timer screen" width="240"> | |

## 3D Printed Stand

A custom **angled desk stand with a pen holder and USB passthrough** designed for the M5Dial. Download the print files from Printables:

[M5Stack Dial Angled Desk Stand with Pen Holder & USB](https://www.printables.com/model/1758751-m5stack-dial-angled-desk-stand-with-pen-holder-usb)

<p align="center">
  <img src="img/3d-printed-case.jpg" alt="3D printed angled desk stand for the M5Stack Dial" width="360">
</p>

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32-S3 (M5StampS3, 8 MB flash) |
| Display | GC9A01 240×240 round SPI LCD |
| Input | Rotary encoder + push button, FT3267 capacitive touch |
| Extras | PCF8563 RTC, buzzer, WS1850S RFID |

## Getting Started

Requires [PlatformIO](https://platformio.org/).

```bash
# Build
pio run

# Build + flash to a connected M5Dial
pio run --target upload

# View serial output
pio device monitor
```

To use the **BLE Volume Controller**, open the app on the dial, then pair with `M5Dial Volume` from your computer's Bluetooth settings.

### Keeping time

On first boot the firmware seeds the RTC from the build timestamp, so the clock is roughly correct right after flashing; you can fine-tune it in **Set Time**. Note the M5Dial has **no RTC backup battery by default** — unplug it for more than a moment and the clock resets (re-seeding to build time on the next boot). To retain time while unplugged, add a battery to the on-board RTC backup socket, or keep the dial powered.

## Project Structure

```
src/
├── main.cpp            # Arduino setup()/loop() entry point
├── hal/                # Hardware abstraction layer (display, touch, encoder, RTC, buzzer)
└── apps/               # MOONCAKE app framework — launcher + individual apps
```

## Credits

Based on M5Stack's [M5Dial-UserDemo](https://github.com/m5stack/M5Dial-UserDemo) and the [MOONCAKE](https://github.com/Forairaaaaa/mooncake) app framework by Forairaaaaa.
