/**
 * @file dev_remote.cpp
 * @brief See dev_remote.h.
 */
#ifdef DEV_REMOTE_ENABLE
#include "dev_remote.h"
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "../ESP32Encoder/src/ESP32Encoder.h"
#include "../../../apps/utilities/time_sync/time_sync.h"
#include <NimBLEDevice.h>

namespace DEV_REMOTE
{
    static LGFX_Sprite*  _canvas  = nullptr;
    static ESP32Encoder* _encoder = nullptr;
    static uint32_t      _press_until = 0;
    static bool          _busy = false;   // Button::read() can re-enter via callbacks

    static char _line[112];
    static int  _pos = 0;

    void attach(LGFX_Sprite* canvas, ESP32Encoder* encoder)
    {
        _canvas  = canvas;
        _encoder = encoder;
    }

    bool buttonForcedDown()
    {
        return (int32_t)(millis() - _press_until) < 0;
    }

    static void _shot()
    {
        if (_canvas == nullptr) return;
        int w = _canvas->width(), h = _canvas->height();
        Serial.printf("~SHOT %d %d\n", w, h);
        const uint8_t* buf = (const uint8_t*)_canvas->getBuffer();
        size_t total = (size_t)w * h * 2;
        for (size_t off = 0; off < total; off += 1024)
        {
            size_t n = total - off < 1024 ? total - off : 1024;
            Serial.write(buf + off, n);
        }
        Serial.flush();
    }

    static void _run(const char* cmd)
    {
        if (strncmp(cmd, "shot", 4) == 0)
            _shot();
        else if (strncmp(cmd, "turn ", 5) == 0 && _encoder)
            _encoder->setCount(_encoder->getCount() + 2 * atoi(cmd + 5));
        else if (strncmp(cmd, "press ", 6) == 0)
            _press_until = millis() + atoi(cmd + 6);
        else if (strncmp(cmd, "wifi ", 5) == 0)
        {
            /* "~wifi SSID [PASS]" — store credentials and sync now, for
             * exercising TIME_SYNC without the phone portal. "~wifi -"
             * forgets them. */
            char ssid[33] = "", pass[65] = "";
            sscanf(cmd + 5, "%32s %64s", ssid, pass);
            if (strcmp(ssid, "-") == 0) ssid[0] = '\0';
            TIME_SYNC::saveCredentials(ssid, pass, nullptr);
            TIME_SYNC::requestSync();
        }
        else if (strncmp(cmd, "ble", 3) == 0)
        {
            bool init = NimBLEDevice::getInitialized();
            NimBLEServer* srv = init ? NimBLEDevice::getServer() : nullptr;
            NimBLEAdvertising* adv = init ? NimBLEDevice::getAdvertising() : nullptr;
            Serial.printf("~BLE init=%d addr=%s server=%d conns=%d adv=%d\n",
                          (int)init,
                          init ? NimBLEDevice::getAddress().toString().c_str() : "-",
                          srv != nullptr, srv ? (int)srv->getConnectedCount() : -1,
                          adv ? (int)adv->isAdvertising() : -1);
        }
        else if (strncmp(cmd, "mem", 3) == 0)
            Serial.printf("~MEM free=%u largest=%u min=%u\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    }

    void poll()
    {
        if (_busy) return;
        _busy = true;
        /* Only consume input that is ours: a line has to start with '~'. */
        while (Serial.available())
        {
            if (_pos == 0 && Serial.peek() != '~') break;
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r')
            {
                _line[_pos] = '\0';
                if (_pos > 1) _run(_line + 1);
                _pos = 0;
            }
            else if (_pos < (int)sizeof(_line) - 1)
                _line[_pos++] = c;
        }
        _busy = false;
    }
}
#endif
