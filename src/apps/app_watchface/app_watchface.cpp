/**
 * @file app_watchface.cpp
 * @brief SCOPE watch face — see app_watchface.h for the concept and controls.
 *
 * Ported from the design prototype in design/watchface-scope.html. Rendering
 * mirrors that canvas 1:1 on the 240x240 sprite; the palette / blend() and the
 * Font7 chromatic time readout are reused from cyber_ui.
 */
#include "app_watchface.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include <math.h>


using namespace MOONCAKE::USER_APP;


static const float DEG = 0.01745329f;
static const int   CX  = 120;
static const int   CY  = 120;

static const char* DOW[7]  = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

/* Bounds-safe lookups: an unset RTC yields tm_mon == -1 (month register 0),
 * and C's `% n` keeps the sign of a negative operand, so plain `x % n` does
 * NOT guarantee a valid index. Clamp explicitly. */
static const char* dow_name(int w) { return (w >= 0 && w < 7)  ? DOW[w] : "---"; }
static const char* mon_name(int m) { return (m >= 0 && m < 12) ? MON[m] : "---"; }


/* ---- input helpers (same idiom as app_stopwatch) ---- */

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


/* ---- date maths ---- */

static int _days_in_month(int mon /*0-11*/, int year)
{
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon < 0 || mon > 11)   // unset/garbage RTC
        return 30;
    if (mon == 1)   // February
    {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return d[mon];
}

static int _day_of_year(const tm& t)
{
    int doy = t.tm_mday;
    for (int m = 0; m < t.tm_mon; m++)
        doy += _days_in_month(m, t.tm_year);
    return doy;
}


/* ---- drawing (mirrors design/watchface-scope.html) ---- */

static void wf_background(LGFX_Sprite* c, uint32_t accent)
{
    /* Faint accent tint at the top fading to near-black, then CRT scanlines. */
    uint32_t top = CYBER::blend(accent, CYBER::BG, 0.10f);
    for (int y = 0; y < 240; y += 2)
        c->fillRect(0, y, 240, 2, CYBER::blend(top, CYBER::BG, y / 239.0f));
    for (int y = 0; y < 240; y += 4)
        c->drawFastHLine(0, y, 240, CYBER::SCANLINE);
}

/* 60-tick rim, hour brackets, neon edge. litSec (0..60) marks the hot tick. */
static void wf_rim(LGFX_Sprite* c, float litSec, uint32_t accent)
{
    c->drawCircle(CX, CY, 117, CYBER::blend(accent, CYBER::BG, 0.55f));
    c->drawCircle(CX, CY, 114, CYBER::blend(accent, CYBER::BG, 0.20f));

    for (int i = 0; i < 60; i++)
    {
        bool  major = (i % 5 == 0);
        float ang   = (-90.0f + i * 6.0f) * DEG;
        float d1    = fmodf((i - litSec) + 60.0f, 60.0f);
        float d2    = fmodf((litSec - i) + 60.0f, 60.0f);
        float dist  = fminf(d1, d2);

        int r1 = major ? 106 : 110, r2 = 116;
        bool hot = (litSec >= 0.0f && dist < 0.9f);
        uint32_t col;
        if (hot)
            col = CYBER::blend(CYBER::WHITE, accent, 0.5f * (1.0f - dist / 0.9f));
        else
            col = major ? CYBER::blend(accent, CYBER::BG, 0.42f) : CYBER::BORDER;

        float ca = cosf(ang), sa = sinf(ang);
        int x1 = CX + (int)(r1 * ca), y1 = CY + (int)(r1 * sa);
        int x2 = CX + (int)(r2 * ca), y2 = CY + (int)(r2 * sa);
        c->drawLine(x1, y1, x2, y2, col);
        if (major || hot)   // thicken: parallel tangential line
        {
            int ox = (int)(-sa), oy = (int)(ca);
            c->drawLine(x1 + ox, y1 + oy, x2 + ox, y2 + oy, col);
        }
    }

    /* Hour brackets at the four diagonals (the hudFrame motif). */
    uint32_t br = CYBER::blend(accent, CYBER::BG, 0.5f);
    const float diag[4] = {45.0f, 135.0f, 225.0f, 315.0f};
    for (int k = 0; k < 4; k++)
    {
        float a = diag[k] * DEG, r = 100.0f, len = 9.0f;
        int bx = CX + (int)(r * cosf(a)), by = CY + (int)(r * sinf(a));
        float tx = -sinf(a), ty = cosf(a);
        c->drawLine(bx - (int)(tx * len), by - (int)(ty * len),
                    bx + (int)(tx * len), by + (int)(ty * len), br);
    }
}

/* Phosphor trail behind the sweep head. Drawn UNDER the rim so it never
 * occludes the ticks/brackets. We can't do true alpha, so a fill is a solid
 * cyan-over-BG blend that reads as glow only while it stays clearly brighter
 * than the background — the faint tail sectors are skipped rather than painted
 * as a dark wedge over the scene. sec in [0,60). */
static void wf_sweep_trail(LGFX_Sprite* c, float sec, uint32_t accent)
{
    float ang_deg = -90.0f + sec * 6.0f;   // degrees, top = -90, clockwise
    const int   slices = 22;
    const float trail  = 58.0f;            // phosphor persistence, degrees

    for (int k = 0; k < slices; k++)
    {
        float fade = 1.0f - (float)k / slices;
        float a    = 0.30f * fade * fade;   // bright glow behind the head
        if (a < 0.11f) continue;            // stop before it darkens — no black tail
        float a1 = ang_deg - trail * k / slices;
        float a0 = ang_deg - trail * (k + 1) / slices;
        c->fillArc(CX, CY, 2, 112, a0, a1, CYBER::blend(accent, CYBER::BG, a));
    }
}

/* Leading sweep line + bright head. Drawn OVER the rim so the head stays crisp. */
static void wf_sweep_head(LGFX_Sprite* c, float sec, uint32_t accent)
{
    float ang = (-90.0f + sec * 6.0f) * DEG, ca = cosf(ang), sa = sinf(ang);
    for (int s = 0; s < 10; s++)
    {
        float t0 = s / 10.0f, t1 = (s + 1) / 10.0f;
        float r0 = 14.0f + (113.0f - 14.0f) * t0;
        float r1 = 14.0f + (113.0f - 14.0f) * t1;
        uint32_t col = CYBER::blend(CYBER::WHITE, accent, t0);
        col = CYBER::blend(col, CYBER::BG, 0.30f + 0.70f * t0);
        c->drawLine(CX + (int)(r0 * ca), CY + (int)(r0 * sa),
                    CX + (int)(r1 * ca), CY + (int)(r1 * sa), col);
    }

    int gx = CX + (int)(113 * ca), gy = CY + (int)(113 * sa);
    c->fillCircle(gx, gy, 4, CYBER::blend(accent, CYBER::BG, 0.45f));
    c->fillSmoothCircle(gx, gy, 2, CYBER::WHITE);
}

static void wf_text(LGFX_Sprite* c, const char* t, int y, uint32_t col, int size = 1)
{
    c->setFont(&fonts::Font0);
    c->setTextSize(size);
    c->setTextColor(col);
    c->setTextDatum(textdatum_t::middle_center);
    c->drawString(t, CX, y);
}


/* ---- views ---- */

void Watchface::_render_time(float sec_float)
{
    LGFX_Sprite* c = _data.hal->canvas;
    const uint32_t accent = CYBER::CYAN;

    wf_background(c, accent);
    wf_sweep_trail(c, sec_float, accent);   // under the rim
    wf_rim(c, sec_float, accent);
    wf_sweep_head(c, sec_float, accent);    // over the rim

    /* Eyebrow: amber "TOP OF HOUR" pulse at :00, otherwise a quiet status. */
    if (_data.now.tm_min == 0)
        wf_text(c, "- T O P   O F   H O U R -", 56, CYBER::AMBER);
    else
        wf_text(c, "S Y S  -  O N L I N E", 56, CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.85f));

    /* Centre HH:MM (Font7 + chromatic ghosts); colon blinks on the half-second.
     * On open it scrambles into place (shared boot-in decrypt). */
    uint32_t bootMs = millis() - _data.boot_start;
    char big[8];
    bool colon = (millis() - _data.sec_epoch) < 500;
    snprintf(big, sizeof(big), "%02d%c%02d",
             _data.now.tm_hour, colon ? ':' : ' ', _data.now.tm_min);
    char shown[8];
    CYBER::scrambleTime(shown, big, bootMs);
    CYBER::bigTime(c, shown, CYBER::WHITE, 1.7f);   // livelier glitch on the home face

    /* Weekday + date, then the quiet seconds numeral at the bottom. */
    char date[20];
    snprintf(date, sizeof(date), "%s   %02d %s",
             dow_name(_data.now.tm_wday), _data.now.tm_mday, mon_name(_data.now.tm_mon));
    wf_text(c, date, 166, CYBER::blend(accent, CYBER::BG, 0.48f), 2);

    char secs[4];
    snprintf(secs, sizeof(secs), "%02d", _data.now.tm_sec);
    wf_text(c, secs, 190, CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.9f), 2);

    _canvas_update();
}

void Watchface::_render_date()
{
    LGFX_Sprite* c = _data.hal->canvas;
    const uint32_t accent = CYBER::AMBER;   // amber = the secondary/info layer

    wf_background(c, accent);
    wf_rim(c, -1.0f, accent);               // dim rim, no hot tick

    /* Rim arc = progress through the month. */
    int   dim   = _days_in_month(_data.now.tm_mon, _data.now.tm_year);
    float frac  = dim > 0 ? (float)(_data.now.tm_mday - 1) / (float)dim : 0.0f;
    float sweep_to = -90.0f + frac * 360.0f;
    if (sweep_to > -90.0f)
        c->fillArc(CX, CY, 108, 112, -90.0f, sweep_to, accent);
    c->fillSmoothCircle(CX + (int)(110 * cosf(sweep_to * DEG)),
                        CY + (int)(110 * sinf(sweep_to * DEG)), 3, CYBER::WHITE);

    wf_text(c, "D A T E", 46, CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.9f));

    /* Big weekday (efontCN_24 — Font7 has no letters). */
    c->setFont(GUI_FONT_CN_BIG);
    c->setTextSize(2);
    c->setTextColor(CYBER::WHITE);
    c->setTextDatum(textdatum_t::middle_center);
    c->drawString(dow_name(_data.now.tm_wday), CX, 110);

    char line[24];
    snprintf(line, sizeof(line), "%02d %s %04d",
             _data.now.tm_mday, mon_name(_data.now.tm_mon), _data.now.tm_year);
    wf_text(c, line, 150, CYBER::blend(accent, CYBER::BG, 0.85f), 2);

    char doy[16];
    snprintf(doy, sizeof(doy), "DAY %d / %d", _day_of_year(_data.now),
             _days_in_month(1, _data.now.tm_year) == 29 ? 366 : 365);
    wf_text(c, doy, 184, CYBER::blend(CYBER::DIMTEXT, CYBER::BG, 0.9f));

    /* How to get back — turn or press both return to the time view. */
    wf_text(c, "TURN OR PRESS = BACK", 204, CYBER::blend(accent, CYBER::BG, 0.5f));

    _canvas_update();
}


/* ---- lifecycle ---- */

void Watchface::onSetup()
{
    setAppName("Watchface");
    setAllowBgRunning(false);

    WATCHFACE::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}

void Watchface::onCreate()
{
    _log("onCreate");
    _data.last_raw = _data.hal->encoder.getCount();
    _data.hal->rtc.getTime(_data.now);
    _data.last_sec   = _data.now.tm_sec;
    _data.sec_epoch  = millis();
    _data.boot_start = millis();
}

void Watchface::onRunning()
{
    /* Turn the dial -> flip between the time and date views. A single twist
     * spans several detents across frames, so lock out further turns briefly:
     * one twist = one switch, either direction. */
    if (_read_detents(_data.hal, _data.last_raw) != 0 &&
        (int32_t)(millis() - _data.turn_lock) >= 0)
    {
        _data.view      = (_data.view == WATCHFACE::TIME) ? WATCHFACE::DATE : WATCHFACE::TIME;
        _data.turn_lock = millis() + 350;
        if (_data.view == WATCHFACE::DATE)
            _data.date_until = millis() + 12000;   // safety auto-revert
    }

    /* Button: long press exits; short press is "home" -> back to the time. */
    int btn = _read_button(_data.hal);
    if (btn == 2)
    {
        destroyApp();
        return;
    }
    if (btn == 1)
        _data.view = WATCHFACE::TIME;

    /* Safety: don't get stuck on the date view. */
    if (_data.view == WATCHFACE::DATE && (int32_t)(millis() - _data.date_until) >= 0)
        _data.view = WATCHFACE::TIME;

    /* ~30 fps: read the RTC, interpolate seconds for a smooth sweep, render. */
    if (millis() - _data.last_render >= 33)
    {
        _data.last_render = millis();

        _data.hal->rtc.getTime(_data.now);
        if (_data.now.tm_sec != _data.last_sec)
        {
            _data.last_sec  = _data.now.tm_sec;
            _data.sec_epoch = millis();
        }

        if (_data.view == WATCHFACE::DATE)
        {
            _render_date();
        }
        else
        {
            float frac = (millis() - _data.sec_epoch) / 1000.0f;
            if (frac > 1.0f) frac = 1.0f;
            _render_time((float)_data.last_sec + frac);
        }
    }

    delay(4);
}

void Watchface::onDestroy() { _log("onDestroy"); }
