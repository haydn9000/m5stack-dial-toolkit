/**
 * @file app_pomodoro.cpp
 * @brief Cyberpunk Pomodoro focus timer.
 *
 *  25 min focus -> 5 min break, x4, then a 15 min long break, repeating.
 *
 *  Controls
 *   - Short press : start / pause / resume
 *   - Turn dial   : (when not running) switch phase manually
 *   - Long press  : exit to menu
 *  Phases auto-advance and auto-start when a block completes.
 */
#include "app_pomodoro.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"


using namespace MOONCAKE::USER_APP;


static const uint32_t PHASE_SECONDS[3] = { 25 * 60, 5 * 60, 15 * 60 };
static const char*    PHASE_LABEL[3]   = { "FOCUS", "BREAK", "LONG BREAK" };
static const uint32_t PHASE_COLOR[3]   = { CYBER::CYAN, CYBER::GREEN, CYBER::AMBER };
static const int      WORK_TARGET      = 4;


static int _read_button(HAL::HAL* hal)
{
    if (hal->encoder.btn.read())
        return 0;
    uint32_t t0 = millis();
    while (!hal->encoder.btn.read())
        delay(5);
    return (millis() - t0 > 700) ? 2 : 1;
}

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


uint32_t Pomodoro::_phase_duration(POMODORO::Phase_t p)
{
    return PHASE_SECONDS[p] * 1000u;
}


uint32_t Pomodoro::_remaining_ms()
{
    if (_data.run == POMODORO::RUNNING)
    {
        uint32_t now = millis();
        return (now >= _data.end_ts) ? 0 : (_data.end_ts - now);
    }
    if (_data.run == POMODORO::PAUSED)
        return _data.remaining;
    return _phase_duration(_data.phase);   // IDLE
}


void Pomodoro::_advance()
{
    if (_data.phase == POMODORO::WORK)
    {
        if (_data.work_done < WORK_TARGET)
            _data.work_done++;
        _data.phase = (_data.work_done >= WORK_TARGET) ? POMODORO::LONG_BREAK
                                                       : POMODORO::SHORT_BREAK;
    }
    else if (_data.phase == POMODORO::SHORT_BREAK)
    {
        _data.phase = POMODORO::WORK;
    }
    else   // LONG_BREAK done -> new cycle
    {
        _data.phase     = POMODORO::WORK;
        _data.work_done = 0;
    }

    /* Auto-start the next block */
    _data.duration_ms = _phase_duration(_data.phase);
    _data.end_ts      = millis() + _data.duration_ms;
    _data.run         = POMODORO::RUNNING;
}


void Pomodoro::_render()
{
    uint32_t r = _remaining_ms();
    uint32_t total_s = (r + 999) / 1000;
    uint32_t m = (total_s / 60) % 100;
    uint32_t s = total_s % 60;

    uint32_t accent = PHASE_COLOR[_data.phase];
    if (_data.run == POMODORO::PAUSED)
        accent = CYBER::blend(accent, CYBER::BG, 0.55f);

    const char* code;
    const char* hint;
    if (_data.run == POMODORO::RUNNING)      { code = "RUN"; hint = "PRESS = PAUSE   HOLD = EXIT"; }
    else if (_data.run == POMODORO::PAUSED)  { code = "PSE"; hint = "PRESS = RESUME   TURN = PHASE"; }
    else                                     { code = "RDY"; hint = "PRESS = START   TURN = PHASE"; }

    uint32_t denom = (_data.run == POMODORO::IDLE) ? _phase_duration(_data.phase)
                                                   : _data.duration_ms;
    float ring = denom ? (float)r / (float)denom : 1.0f;

    uint32_t bootMs = millis() - _data.boot_start;

    char big[12];
    snprintf(big, sizeof(big), "%02u:%02u", (unsigned)m, (unsigned)s);

    LGFX_Sprite* cv = _data.hal->canvas;
    CYBER::hudChrome(cv, PHASE_LABEL[_data.phase], code, accent, bootMs);
    CYBER::progressRing(cv, ring * CYBER::bootProgress(bootMs), accent);

    char shown[12];
    CYBER::scrambleTime(shown, big, bootMs);
    CYBER::bigTime(cv, shown, CYBER::WHITE);

    /* Four session dots: filled = completed focus blocks this cycle */
    for (int i = 0; i < WORK_TARGET; i++)
    {
        int x = 120 + (int)((i - 1.5f) * 18);
        int y = 160;
        if (i < _data.work_done)
            cv->fillSmoothCircle(x, y, 5, CYBER::CYAN);
        else
        {
            cv->fillSmoothCircle(x, y, 5, CYBER::TRACK);
            cv->drawCircle(x, y, 5, CYBER::DIMTEXT);
        }
    }

    CYBER::hint(cv, hint);
    _canvas_update();
}


void Pomodoro::onSetup()
{
    setAppName("Pomodoro");
    setAllowBgRunning(false);

    POMODORO::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}


void Pomodoro::onCreate()
{
    _log("onCreate");
    _data.last_raw   = _data.hal->encoder.getCount();
    _data.boot_start = millis();
    _render();
}


void Pomodoro::onRunning()
{
    /* Dial: switch phase manually while not running */
    int detents = _read_detents(_data.hal, _data.last_raw);
    if (detents != 0 && _data.run != POMODORO::RUNNING)
    {
        int p = (int)_data.phase + (detents > 0 ? 1 : -1);
        p = (p % 3 + 3) % 3;
        _data.phase = (POMODORO::Phase_t)p;
        _data.run   = POMODORO::IDLE;
        _data.hal->buzz.fxTick(detents > 0);
        _render();
    }

    /* Button */
    int btn = _read_button(_data.hal);
    if (btn == 2)
    {
        _data.hal->buzz.noTone();
        destroyApp();
        return;
    }
    if (btn == 1)
    {
        if (_data.run == POMODORO::RUNNING)
        {
            _data.remaining = _remaining_ms();
            _data.run       = POMODORO::PAUSED;
            _data.hal->buzz.fxCancel();
        }
        else if (_data.run == POMODORO::PAUSED)
        {
            _data.end_ts = millis() + _data.remaining;
            _data.run    = POMODORO::RUNNING;
            _data.hal->buzz.fxConfirm();
        }
        else   // IDLE
        {
            _data.duration_ms = _phase_duration(_data.phase);
            _data.end_ts      = millis() + _data.duration_ms;
            _data.run         = POMODORO::RUNNING;
            _data.hal->buzz.fxConfirm();
        }
        _render();
    }

    /* Block completion -> loud chime + auto-advance. Tones sit near the
     * piezo's ~4 kHz resonance so the alert is as loud as possible. */
    if (_data.run == POMODORO::RUNNING && _remaining_ms() == 0)
    {
        _data.hal->buzz.fxComplete();
        _advance();
        _render();
    }

    /* Live refresh */
    if (_data.run == POMODORO::RUNNING && millis() - _data.last_render >= 100)
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


void Pomodoro::onDestroy()
{
    _log("onDestroy");
    _data.hal->buzz.noTone();
}
