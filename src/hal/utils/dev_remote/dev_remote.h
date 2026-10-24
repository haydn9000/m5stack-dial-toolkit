/**
 * @file dev_remote.h
 * @brief Development aid: drive the device and grab screenshots over the
 *        USB serial port, so UI work can be checked without hands on the
 *        dial. Commands are single lines starting with '~' (PC_LINK's JSON
 *        lines start with '{', so the two never collide):
 *
 *          ~shot        -> replies "~SHOT 240 240\n" + 240*240*2 bytes of the
 *                          canvas (RGB565, big-endian, as LovyanGFX stores it)
 *          ~turn N      -> moves the encoder N detents (negative = CCW)
 *          ~press MS    -> holds the encoder button down for MS milliseconds
 *          ~wifi SSID [PASS] -> save TIME_SYNC credentials and sync now ("-" = forget)
 *          ~ble         -> replies "~BLE init=.. conns=.. adv=.." (NimBLE state)
 *          ~mem         -> replies "~MEM free=.. largest=.. min=.." (internal heap)
 *
 *        Polled from Button::read(), which every app calls each frame.
 *        Compiled in only with -DDEV_REMOTE_ENABLE. Host side: tools/dial_remote.py
 */
#pragma once
#include <stdint.h>

namespace lgfx { inline namespace v1 { class LGFX_Sprite; } }
class ESP32Encoder;

namespace DEV_REMOTE
{
#ifdef DEV_REMOTE_ENABLE
    void attach(lgfx::LGFX_Sprite* canvas, ESP32Encoder* encoder);
    void poll();
    bool buttonForcedDown();
#else
    inline void attach(lgfx::LGFX_Sprite*, ESP32Encoder*) {}
    inline void poll() {}
    inline bool buttonForcedDown() { return false; }
#endif
}
