/**
 * @file app_set_time.cpp
 * @brief On-device RTC setter — see app_set_time.h for controls.
 */
#include "app_set_time.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include <math.h>


using namespace MOONCAKE::USER_APP;


static const char* MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char* LABEL[SET_TIME::F_COUNT] = {"YEAR", "MONTH", "DAY", "HOUR", "MINUTE"};


/* Blocking button read: 0 = none, 1 = short press, 2 = long press (>700ms). */
static int _read_button(HAL::HAL* hal)
{
    if (hal->encoder.btn.read())
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

static int _wrap(int x, int n) { return ((x % n) + n) % n; }

static int _days_in_month(int mon /*1-12*/, int year)
{
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon < 1 || mon > 12) return 31;
    if (mon == 2)
    {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return d[mon - 1];
}

/* Day of week (0=Sun) via Sakamoto's algorithm. mon = 1-12. */
static int _day_of_week(int y, int mon, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (mon < 3) y -= 1;
    return ((y + y / 4 - y / 100 + y / 400 + t[mon - 1] + d) % 7 + 7) % 7;
}


void SetTime::_adjust(int delta)
{
    switch (_data.field)
    {
        case SET_TIME::F_YEAR:
            _data.year += delta;
            if (_data.year < 2023) _data.year = 2023;
            if (_data.year > 2099) _data.year = 2099;
            break;
        case SET_TIME::F_MON:  _data.mon  = _wrap(_data.mon - 1 + delta, 12) + 1; break;
        case SET_TIME::F_DAY:
            _data.day = _wrap(_data.day - 1 + delta, _days_in_month(_data.mon, _data.year)) + 1;
            break;
        case SET_TIME::F_HOUR: _data.hour = _wrap(_data.hour + delta, 24); break;
        case SET_TIME::F_MIN:  _data.min  = _wrap(_data.min + delta, 60); break;
    }

    /* Keep the day valid after a month/year change. */
    int dim = _days_in_month(_data.mon, _data.year);
    if (_data.day > dim) _data.day = dim;
}

void SetTime::_save()
{
    tm t = {};
    t.tm_year  = _data.year;          // full year; PCF8563::setTime does -2000
    t.tm_mon   = _data.mon - 1;       // 0-11
    t.tm_mday  = _data.day;
    t.tm_hour  = _data.hour;
    t.tm_min   = _data.min;
    t.tm_sec   = 0;
    t.tm_wday  = _day_of_week(_data.year, _data.mon, _data.day);
    t.tm_isdst = 0;
    _data.hal->rtc.setTime(t);
    _log("RTC set: %04d-%02d-%02d %02d:%02d", _data.year, _data.mon, _data.day,
         _data.hour, _data.min);
}

void SetTime::_render()
{
    LGFX_Sprite*   c      = _data.hal->canvas;
    const uint32_t accent = CYBER::CYAN;

    CYBER::background(c, accent);
    /* Ring encodes progress through the fields. */
    CYBER::progressRing(c, (float)_data.field / (float)(SET_TIME::F_COUNT - 1), accent);
    CYBER::title(c, "SET TIME", accent);
    CYBER::subtitle(c, LABEL[_data.field], CYBER::WHITE);

    /* Big Font7 value for the active field. */
    char big[8];
    if (_data.field == SET_TIME::F_YEAR)      snprintf(big, sizeof(big), "%04d", _data.year);
    else if (_data.field == SET_TIME::F_MON)  snprintf(big, sizeof(big), "%02d", _data.mon);
    else if (_data.field == SET_TIME::F_DAY)  snprintf(big, sizeof(big), "%02d", _data.day);
    else if (_data.field == SET_TIME::F_HOUR) snprintf(big, sizeof(big), "%02d", _data.hour);
    else                                      snprintf(big, sizeof(big), "%02d", _data.min);
    CYBER::bigTime(c, big, CYBER::WHITE);

    /* Full preview so the field always has context. */
    char preview[24];
    snprintf(preview, sizeof(preview), "%04d %s %02d   %02d:%02d",
             _data.year, MON[_data.mon - 1], _data.day, _data.hour, _data.min);
    c->setFont(&fonts::Font0);
    c->setTextSize(1);
    c->setTextColor(CYBER::blend(accent, CYBER::BG, 0.7f));
    c->setTextDatum(textdatum_t::middle_center);
    c->drawString(preview, CYBER::CX, 158);

    CYBER::hint(c, "PRESS = NEXT   HOLD = SAVE");
    _canvas_update();
}


void SetTime::onSetup()
{
    setAppName("SetTime");
    setAllowBgRunning(false);

    SET_TIME::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}

void SetTime::onCreate()
{
    _log("onCreate");
    _data.last_raw = _data.hal->encoder.getCount();

    /* Start from the current RTC time, or sane defaults if it's unset. */
    tm now;
    if (_data.hal->rtc.getTime(now) == ESP_OK && now.tm_year >= 2023 &&
        now.tm_mon >= 0 && now.tm_mon <= 11)
    {
        _data.year = now.tm_year;
        _data.mon  = now.tm_mon + 1;
        _data.day  = (now.tm_mday >= 1 && now.tm_mday <= 31) ? now.tm_mday : 1;
        _data.hour = (now.tm_hour >= 0 && now.tm_hour <= 23) ? now.tm_hour : 0;
        _data.min  = (now.tm_min  >= 0 && now.tm_min  <= 59) ? now.tm_min  : 0;
    }

    _render();
}

void SetTime::onRunning()
{
    int det = _read_detents(_data.hal, _data.last_raw);
    if (det != 0)
    {
        _adjust(det);
        _render();
    }

    int btn = _read_button(_data.hal);
    if (btn == 2)          // hold = save & exit
    {
        _save();
        _data.hal->buzz.tone(6000, 40);
        delay(60);
        _data.hal->buzz.tone(9000, 60);
        destroyApp();
        return;
    }
    if (btn == 1)          // short = next field
    {
        _data.field = (_data.field + 1) % SET_TIME::F_COUNT;
        _data.hal->buzz.tone(7000, 25);
        _render();
    }

    delay(4);
}

void SetTime::onDestroy() { _log("onDestroy"); }
