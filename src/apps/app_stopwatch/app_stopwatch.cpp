/**
 * @file app_stopwatch.cpp
 * @brief Cyberpunk stopwatch.
 *
 *  Controls
 *   - Short press : start / pause / resume
 *   - Turn dial   : reset to 00:00 (only when paused or stopped)
 *   - Long press  : exit to menu
 */
#include "app_stopwatch.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"


using namespace MOONCAKE::USER_APP;


/* Blocking button read: 0 = none, 1 = short press, 2 = long press (>700ms). */
static int _read_button(HAL::HAL* hal)
{
    if (hal->encoder.btn.read())   // HIGH = not pressed
        return 0;
    uint32_t t0 = millis();
    while (!hal->encoder.btn.read())
        delay(5);
    return (millis() - t0 > 700) ? 2 : 1;
}

/* Whole detents turned since last call (half-quad: 2 raw counts = 1 detent). */
static int _read_detents(HAL::HAL* hal, int64_t& last_raw)
{
    int64_t raw   = hal->encoder.getCount();
    int64_t delta = raw - last_raw;
    if (delta <= -2 || delta >= 2)
    {
        int detents = (int)(delta / 2);
        last_raw += (int64_t)detents * 2;
        return detents;
    }
    return 0;
}


uint32_t Stopwatch::_elapsed()
{
    if (_data.state == STOPWATCH::RUNNING)
        return _data.accumulated + (millis() - _data.start_ts);
    return _data.accumulated;
}


void Stopwatch::_render()
{
    uint32_t e  = _elapsed();
    uint32_t cs = (e / 10) % 100;
    uint32_t s  = (e / 1000) % 60;
    uint32_t m  = (e / 60000) % 100;

    uint32_t accent;
    const char* code;
    const char* hint;
    if (_data.state == STOPWATCH::RUNNING) { accent = CYBER::CYAN;  code = "RUN"; hint = "PRESS = PAUSE"; }
    else if (_data.state == STOPWATCH::PAUSED) { accent = CYBER::AMBER; code = "PSE"; hint = "TURN = RESET   HOLD = EXIT"; }
    else { accent = CYBER::blend(CYBER::CYAN, CYBER::BG, 0.55f); code = "RDY"; hint = "PRESS = START   HOLD = EXIT"; }

    /* Ring sweeps once per minute like a second hand */
    float ring = (float)(e % 60000) / 60000.0f;

    uint32_t bootMs = millis() - _data.boot_start;

    char big[12];
    char sub[8];
    snprintf(big, sizeof(big), "%02u:%02u", (unsigned)m, (unsigned)s);
    snprintf(sub, sizeof(sub), ".%02u", (unsigned)cs);

    LGFX_Sprite* cv = _data.hal->canvas;
    CYBER::hudChrome(cv, "STOPWATCH", code, accent, bootMs);
    CYBER::progressRing(cv, ring * CYBER::bootProgress(bootMs), accent);

    char shown[12];
    CYBER::scrambleTime(shown, big, bootMs);
    CYBER::bigTime(cv, shown, CYBER::WHITE);

    if (!CYBER::booting(bootMs))
        CYBER::smallReadout(cv, sub, CYBER::AMBER);   // centiseconds = yellow highlight (default y=160)
    CYBER::hint(cv, hint);
    _canvas_update();
}


void Stopwatch::onSetup()
{
    setAppName("Stopwatch");
    setAllowBgRunning(false);

    STOPWATCH::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}


void Stopwatch::onCreate()
{
    _log("onCreate");
    _data.last_raw   = _data.hal->encoder.getCount();
    _data.boot_start = millis();
    _render();
}


void Stopwatch::onRunning()
{
    /* Reset on dial turn (ignored while running to avoid losing a timing) */
    if (_read_detents(_data.hal, _data.last_raw) != 0)
    {
        if (_data.state != STOPWATCH::RUNNING)
        {
            _data.state       = STOPWATCH::STOPPED;
            _data.accumulated = 0;
            _data.hal->buzz.fxReset();
            _render();
        }
    }

    /* Button */
    int btn = _read_button(_data.hal);
    if (btn == 2)
    {
        destroyApp();
        return;
    }
    if (btn == 1)
    {
        if (_data.state == STOPWATCH::RUNNING)
        {
            _data.accumulated += millis() - _data.start_ts;
            _data.state = STOPWATCH::PAUSED;
            _data.hal->buzz.fxCancel();
        }
        else
        {
            _data.start_ts = millis();
            _data.state    = STOPWATCH::RUNNING;
            _data.hal->buzz.fxConfirm();
        }
        _render();
    }

    /* Live update while running (~60fps centiseconds) */
    if (_data.state == STOPWATCH::RUNNING && millis() - _data.last_render >= 16)
    {
        _data.last_render = millis();
        _render();
    }

    /* Keep the boot-in animating even while idle */
    if (CYBER::booting(millis() - _data.boot_start) && millis() - _data.last_render >= 33)
    {
        _data.last_render = millis();
        _render();
    }

    delay(4);
}


void Stopwatch::onDestroy()
{
    _log("onDestroy");
}
