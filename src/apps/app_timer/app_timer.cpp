/**
 * @file app_timer.cpp
 * @brief Cyberpunk countdown timer.
 *
 *  Controls
 *   - SETTING : turn dial to set time, short press to start
 *   - RUNNING : short press to pause
 *   - PAUSED  : short press to resume, turn dial to reset to SETTING
 *   - FINISHED: alarm sounds; short press to dismiss back to SETTING
 *   - Long press: exit to menu (any state)
 */
#include "app_timer.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"


using namespace MOONCAKE::USER_APP;


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

/* Coarser steps for longer durations so setting stays quick yet precise. */
static int _step_for(int set_s)
{
    if (set_s < 60)  return 5;
    if (set_s < 300) return 15;
    if (set_s < 1200) return 30;
    return 60;
}


uint32_t Timer::_remaining_ms()
{
    if (_data.state == TIMER_APP::RUNNING)
    {
        uint32_t now = millis();
        return (now >= _data.end_ts) ? 0 : (_data.end_ts - now);
    }
    if (_data.state == TIMER_APP::PAUSED)
        return _data.remaining;
    if (_data.state == TIMER_APP::FINISHED)
        return 0;
    return (uint32_t)_data.set_seconds * 1000u;
}


void Timer::_render()
{
    uint32_t r = _remaining_ms();
    uint32_t total_s = (r + 999) / 1000;          // ceil so it hits 00:00 only at end
    uint32_t m = (total_s / 60) % 100;
    uint32_t s = total_s % 60;

    uint32_t accent;
    const char* sub  = "";
    const char* hint = "";

    switch (_data.state)
    {
        case TIMER_APP::SETTING:
            accent = CYBER::AMBER;
            sub  = "SET";
            hint = "TURN = SET   PRESS = START";
            break;
        case TIMER_APP::RUNNING:
            accent = CYBER::CYAN;
            sub  = "RUNNING";
            hint = "PRESS = PAUSE   HOLD = EXIT";
            break;
        case TIMER_APP::PAUSED:
            accent = CYBER::MAGENTA;
            sub  = "PAUSED";
            hint = "PRESS = RESUME   TURN = RESET";
            break;
        case TIMER_APP::FINISHED:
        default:
            accent = ((millis() / 300) % 2) ? CYBER::RED
                                            : CYBER::blend(CYBER::RED, CYBER::BG, 0.3f);
            sub  = "TIME!";
            hint = "PRESS = DISMISS   HOLD = EXIT";
            break;
    }

    float ring;
    if (_data.state == TIMER_APP::SETTING)
        ring = 1.0f;
    else if (_data.state == TIMER_APP::FINISHED || _data.duration_ms == 0)
        ring = (_data.state == TIMER_APP::FINISHED) ? 1.0f : 0.0f;
    else
        ring = (float)r / (float)_data.duration_ms;

    char big[12];
    snprintf(big, sizeof(big), "%02u:%02u", (unsigned)m, (unsigned)s);

    CYBER::background(_data.hal->canvas, accent);
    CYBER::progressRing(_data.hal->canvas, ring, accent);
    CYBER::title(_data.hal->canvas, "TIMER", accent);
    CYBER::subtitle(_data.hal->canvas, sub, accent);
    CYBER::bigTime(_data.hal->canvas, big, CYBER::WHITE);
    CYBER::hint(_data.hal->canvas, hint);
    _canvas_update();
}


void Timer::onSetup()
{
    setAppName("Timer");
    setAllowBgRunning(false);

    TIMER_APP::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}


void Timer::onCreate()
{
    _log("onCreate");
    _data.last_raw = _data.hal->encoder.getCount();
    _render();
}


void Timer::onRunning()
{
    int detents = _read_detents(_data.hal, _data.last_raw);

    /* Dial: set duration (SETTING) or reset (PAUSED) */
    if (detents != 0)
    {
        if (_data.state == TIMER_APP::SETTING)
        {
            int step = _step_for(_data.set_seconds);
            _data.set_seconds += detents * step;
            if (_data.set_seconds < 0)    _data.set_seconds = 0;
            if (_data.set_seconds > 5999) _data.set_seconds = 5999;   // 99:59
            _render();
        }
        else if (_data.state == TIMER_APP::PAUSED)
        {
            _data.state = TIMER_APP::SETTING;
            _data.hal->buzz.tone(4000, 25);
            _render();
        }
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
        switch (_data.state)
        {
            case TIMER_APP::SETTING:
                if (_data.set_seconds > 0)
                {
                    _data.duration_ms = (uint32_t)_data.set_seconds * 1000u;
                    _data.end_ts      = millis() + _data.duration_ms;
                    _data.state       = TIMER_APP::RUNNING;
                    _data.hal->buzz.tone(8000, 30);
                }
                break;
            case TIMER_APP::RUNNING:
                _data.remaining = _remaining_ms();
                _data.state     = TIMER_APP::PAUSED;
                _data.hal->buzz.tone(5000, 30);
                break;
            case TIMER_APP::PAUSED:
                _data.end_ts = millis() + _data.remaining;
                _data.state  = TIMER_APP::RUNNING;
                _data.hal->buzz.tone(8000, 30);
                break;
            case TIMER_APP::FINISHED:
                _data.hal->buzz.noTone();
                _data.state = TIMER_APP::SETTING;
                break;
        }
        _render();
    }

    /* Countdown completion */
    if (_data.state == TIMER_APP::RUNNING && _remaining_ms() == 0)
    {
        _data.state    = TIMER_APP::FINISHED;
        _data.alarm_ts = 0;   // beep immediately
    }

    /* Alarm pattern while finished — a loud double-chirp near the piezo's
     * resonant frequency (~4 kHz, where it's loudest) so it carries. */
    if (_data.state == TIMER_APP::FINISHED && millis() >= _data.alarm_ts)
    {
        _data.hal->buzz.tone(4000, 180);
        delay(200);
        _data.hal->buzz.tone(4500, 180);
        _data.alarm_ts = millis() + 450;
    }

    /* Live refresh */
    bool live = (_data.state == TIMER_APP::RUNNING || _data.state == TIMER_APP::FINISHED);
    if (live && millis() - _data.last_render >= 60)
    {
        _data.last_render = millis();
        _render();
    }

    delay(4);
}


void Timer::onDestroy()
{
    _log("onDestroy");
    _data.hal->buzz.noTone();
}
