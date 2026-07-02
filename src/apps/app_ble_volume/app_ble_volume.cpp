/**
 * @file app_ble_volume.cpp
 * @brief BLE HID volume controller — rotary encoder controls system volume on
 *        any paired computer or phone via standard BLE media keys.
 *
 *        Encoder CW  → KEY_MEDIA_VOLUME_UP
 *        Encoder CCW → KEY_MEDIA_VOLUME_DOWN
 *        Button      → KEY_MEDIA_MUTE (toggle)
 */
#include "app_ble_volume.h"
#include "../common_define.h"
#include <algorithm>
#include <BleKeyboard.h>
#include <NimBLEDevice.h>  /* for NimBLEDevice::isInitialized() */


/* File-scope BleKeyboard instance — one at a time, apps don't run concurrently */
static BleKeyboard bleKeyboard("M5Dial Volume", "M5Stack", 100);
/* begin() must only be called once; the BLE stack stays alive between app open/close */
static bool _ble_started = false;

/* Persisted across app open/close so the dial remembers the last volume/mute */
static int  _saved_volume = 50;
static bool _saved_muted  = false;
static bool _has_saved    = false;

/* Most OSes (incl. Windows) move system volume by 2% per media-key press */
static const int VOLUME_STEP = 2;


using namespace MOONCAKE::USER_APP;


void BLE_Volume::_ble_init()
{
    /* If ble_demo_stop() previously called deinit(), the stack is down.
     * Reset the guard so begin() is called again to bring it back up. */
    if (_ble_started && !NimBLEDevice::getInitialized()) {
        _ble_started = false;
    }
    if (!_ble_started)
    {
        _log("Starting BLE Volume HID");
        bleKeyboard.begin();
        _ble_started = true;
    }
    else if (!bleKeyboard.isConnected())
    {
        /* The BleKeyboard library does NOT restart advertising on disconnect
         * when built with NimBLE, so re-entering the app while disconnected
         * would otherwise stay stuck on "searching". Kick it back on. */
        _log("Restarting BLE advertising");
        NimBLEDevice::startAdvertising();
    }
}


void BLE_Volume::onSetup()
{
    setAppName("BLE_Volume");
    setAllowBgRunning(false);

    BLE_VOLUME::Data_t default_data;
    _data = default_data;

    /* Restore last volume/mute if the app was opened before this session */
    if (_has_saved)
    {
        _data.volume = _saved_volume;
        _data.muted  = _saved_muted;
    }
    else
    {
        /* First launch after power-on: start silent (volume 0, muted) */
        _data.volume = 0;
        _data.muted  = true;
    }

    /* Volume 0 always means muted */
    if (_data.volume <= 0)
    {
        _data.volume = 0;
        _data.muted  = true;
    }

    _data.hal = (HAL::HAL*)getUserData();
}


void BLE_Volume::onCreate()
{
    _log("onCreate");
    _ble_init();

    /* Suppress the global per-notch encoder beep — this app only beeps at the
       extremes (0/mute or 100), handled manually in onRunning(). */
    _saved_move_cb       = _data.hal->encoder._move_callback;
    _saved_move_userdata = _data.hal->encoder._user_data;
    _data.hal->encoder._move_callback = nullptr;
    _data.hal->encoder._user_data     = nullptr;

    /* Sync our raw-count baseline so the first turn measures from here */
    _last_raw = _data.hal->encoder.getCount();

    /* Draw initial page */
    _gui.renderPage(bleKeyboard.isConnected(), _data.volume, _data.muted);
}


void BLE_Volume::onRunning()
{
    /* --- Encoder: volume up / down ---
     * Read the raw quadrature count directly and only act on whole detents
     * (2 counts each on this half-quad encoder). This avoids the half-step
     * truncation in readCount()/getDirection() that could register a spurious
     * reverse tick from contact bounce when held against the extremes. */
    int64_t raw   = _data.hal->encoder.getCount();
    int64_t delta = raw - _last_raw;
    if (delta >= 2 || delta <= -2)
    {
        int  detents = (int)(delta / 2);             /* signed whole detents */
        _last_raw   += (int64_t)detents * 2;         /* consume them */
        bool up      = detents > 0;                  /* raw increase = volume up */
        int  clicks  = up ? detents : -detents;      /* magnitude */

        if (_data.muted)
        {
            /* While muted: turning UP un-mutes; turning DOWN lowers the level
             * but stays muted. */
            if (up)
            {
                if (_data.volume == 0)
                {
                    /* Zero-mute: the host is already at actual 0 (not OS-muted),
                     * so just raise the level back off zero with volume-up keys. */
                    int presses = std::min(clicks, 100 / VOLUME_STEP);
                    if (bleKeyboard.isConnected())
                    {
                        for (int i = 0; i < presses; i++)
                        {
                            bleKeyboard.press(KEY_MEDIA_VOLUME_UP);
                            bleKeyboard.release(KEY_MEDIA_VOLUME_UP);
                            delay(4);
                        }
                    }
                    _data.volume += presses * VOLUME_STEP;
                }
                else
                {
                    /* Button-mute at a real level: toggle OS mute off, keep level. */
                    if (bleKeyboard.isConnected())
                    {
                        bleKeyboard.press(KEY_MEDIA_MUTE);
                        bleKeyboard.release(KEY_MEDIA_MUTE);
                    }
                }
                _data.muted = false;
            }
            else
            {
                /* Turning DOWN while muted: lower the underlying level but stay
                 * muted. Volume-down keys still adjust the host's level even
                 * while it's OS-muted. Always send a key per detent so that
                 * once we bottom out at 0 we keep driving the host slider to
                 * absolute zero (the host clamps below 0). */
                if (bleKeyboard.isConnected())
                {
                    for (int i = 0; i < clicks; i++)
                    {
                        bleKeyboard.press(KEY_MEDIA_VOLUME_DOWN);
                        bleKeyboard.release(KEY_MEDIA_VOLUME_DOWN);
                        delay(4);
                    }
                }
                _data.volume -= clicks * VOLUME_STEP;
                if (_data.volume < 0)
                    _data.volume = 0;
            }
        }
        else if (up)
        {
            /* Volume up. Each keypress moves the host by VOLUME_STEP%, so move
             * the on-screen value by the same amount to stay in sync. */
            int presses = std::min(clicks, (100 - _data.volume) / VOLUME_STEP);
            if (bleKeyboard.isConnected())
            {
                for (int i = 0; i < presses; i++)
                {
                    bleKeyboard.press(KEY_MEDIA_VOLUME_UP);
                    bleKeyboard.release(KEY_MEDIA_VOLUME_UP);
                    delay(4);
                }
            }
            _data.volume += presses * VOLUME_STEP;
        }
        else
        {
            /* Volume down */
            int presses = std::min(clicks, _data.volume / VOLUME_STEP);
            if (bleKeyboard.isConnected())
            {
                for (int i = 0; i < presses; i++)
                {
                    bleKeyboard.press(KEY_MEDIA_VOLUME_DOWN);
                    bleKeyboard.release(KEY_MEDIA_VOLUME_DOWN);
                    delay(4);
                }
            }
            _data.volume -= presses * VOLUME_STEP;

            /* Reaching zero mutes. The normal volume-down press above already
             * stepped the host to its lowest level, so no extra burst here. */
            if (_data.volume <= 0)
            {
                _data.volume = 0;
                _data.muted = true;
            }
        }

        /* Blip only at the extreme ends (0/mute or 100), identical for both. */
        if (_data.volume == 0 || _data.volume == 100)
            _data.hal->buzz.fxTick(true);

        /* Persist for next time the app is opened */
        _saved_volume = _data.volume;
        _saved_muted  = _data.muted;
        _has_saved    = true;
    }


    /* --- Button: long press (>800ms) = exit, short press = mute toggle --- */
    if (!_data.hal->encoder.btn.read())
    {
        uint32_t press_start = millis();
        while (!_data.hal->encoder.btn.read())
            delay(5);
        uint32_t held_ms = millis() - press_start;

        if (held_ms > 800)
        {
            /* Long press: return to launcher */
            destroyApp();
            return;
        }

        /* Short press: mute toggle */
        if (bleKeyboard.isConnected())
        {
            bleKeyboard.press(KEY_MEDIA_MUTE);
            bleKeyboard.release(KEY_MEDIA_MUTE);
        }
        _data.muted = !_data.muted;

        /* Persist mute state */
        _saved_muted = _data.muted;
        _has_saved   = true;

        /* Immediate GUI update on button press */
        _gui.renderPage(bleKeyboard.isConnected(), _data.volume, _data.muted);
        _data.page_update_time_count = millis();
    }

    /* Small yield keeps the BLE FreeRTOS tasks healthy */
    delay(10);

    /* --- Disconnected? Make sure we keep advertising so the host can find us.
     * (BleKeyboard's NimBLE build does not auto-restart advertising.) --- */
    if (!bleKeyboard.isConnected())
    {
        static uint32_t advertise_time_count = 0;
        if ((millis() - advertise_time_count) > 2000)
        {
            NimBLEDevice::startAdvertising();
            advertise_time_count = millis();
        }
    }


    /* --- Throttled GUI refresh --- */
    if ((millis() - _data.page_update_time_count) > _data.page_update_interval)
    {
        _gui.renderPage(bleKeyboard.isConnected(), _data.volume, _data.muted);
        _data.page_update_time_count = millis();
    }
}


void BLE_Volume::onDestroy()
{
    _log("onDestroy");

    /* Restore the global encoder beep callback */
    _data.hal->encoder._move_callback = _saved_move_cb;
    _data.hal->encoder._user_data     = _saved_move_userdata;

    /* Remember the last volume/mute so re-opening restores it */
    _saved_volume = _data.volume;
    _saved_muted  = _data.muted;
    _has_saved    = true;
    /* Leave the BLE stack running — the static bleKeyboard stays connected
     * so the host doesn't see a disconnect just because we went back to the
     * launcher. */
}
