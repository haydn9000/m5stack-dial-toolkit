/**
 * @file app_template.cpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-07-28
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include "app_set_brightness.h"
#include "../common_define.h"
#include <Preferences.h>


using namespace MOONCAKE::USER_APP;


void Set_Brightness::onSetup()
{
    // setAppName("Set_Brightness");
    setAllowBgRunning(false);

    /* Copy default value */
    SET_BRIGHTNESS::Data_t default_data;
    _data = default_data;

    _data.hal = (HAL::HAL*)getUserData();
}


/* Life cycle */
void Set_Brightness::onCreate()
{
    _log("onCreate");

    /* Get current brightness (0..255) as a percentage */
    uint8_t cur = _data.hal->display.getBrightness();
    _data.pct = (cur * 100 + 127) / 255;
    _log("get brightness: %d (%d%%)", cur, _data.pct);

    /* Update page for the first time */
    _gui.renderPage(cur);

    /* Anim init + encoder baseline */
    _data.brightness_anim.setAnim(LVGL::ease_out, cur, cur, 0);
    _data.last_raw = _data.hal->encoder.getCount();
}


void Set_Brightness::onRunning()
{
    /* Turn the dial — read whole detents (half-quad: 2 raw counts = 1 detent)
     * so one click = one step. Each detent moves a clearly-visible amount;
     * turning faster stacks detents per loop for natural acceleration. */
    int64_t raw   = _data.hal->encoder.getCount();
    int64_t delta = raw - _data.last_raw;
    if (delta <= -2 || delta >= 2)
    {
        int detents = (int)(delta / 2);
        _data.last_raw += (int64_t)detents * 2;

        _data.pct += detents;                // +/-1% per detent; CW (raw up) = brighter
        if (_data.pct > 100) _data.pct = 100;
        if (_data.pct < 0)   _data.pct = 0;

        int mapped = (_data.pct * 255 + 50) / 100;   // percent -> 0..255 backlight
        _data.hal->buzz.fxTick(detents > 0);   // click feedback
        _log("set to: %d%% (%d)", _data.pct, mapped);
        _gui.renderPage((uint8_t)mapped);

        _data.brightness_anim.setAnim(LVGL::ease_out,
            _data.brightness_anim.getValue(millis()), mapped, 400);
        _data.brightness_anim.resetTime(millis());
    }


    /* Update brightness */
    if (!_data.brightness_anim.isFinished(millis()))
    {
        _data.hal->display.setBrightness(_data.brightness_anim.getValue(millis()));
    }


    /* If button pressed */
    if (!_data.hal->encoder.btn.read())
    {
        /* Hold until button release */
        while (!_data.hal->encoder.btn.read())
            delay(5);

        /* Bye */
        destroyApp();
    }
}


void Set_Brightness::onDestroy()
{
    _log("onDestroy");

    /* Persist brightness across power cycles (NVS, stored as 0..255) */
    int mapped = (_data.pct * 255 + 50) / 100;
    Preferences prefs;
    prefs.begin("settings", false);
    prefs.putInt("bright", mapped);
    prefs.end();
    _log("saved brightness: %d (%d%%)", mapped, _data.pct);
}
