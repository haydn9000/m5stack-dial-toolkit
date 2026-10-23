/**
 * @file app_claude_usage.cpp
 * @brief See app_claude_usage.h for the concept and controls.
 */
#include "app_claude_usage.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include "../utilities/pc_link/pc_link.h"
#include <string.h>


using namespace MOONCAKE::USER_APP;

static const uint32_t CLAUDE_ACCENT = 0xD97757;   // coral — Claude's own identity, distinct from default cyan

/* The gauge fills almost the whole screen (a thin rim near the true edge),
 * which frees the whole interior for the big number. */
static const int RING_CX = 120, RING_CY = 120, RING_R = 103;

/* Blocking button read: 0 = none, 1 = short press, 2 = long press (>700ms).
 * Same idiom as app_watchface/app_stopwatch. */
static int _read_button(HAL::HAL* hal)
{
    if (hal->encoder.btn.read())
        return 0;
    uint32_t t0 = millis();
    while (!hal->encoder.btn.read())
        delay(5);
    return (millis() - t0 > 700) ? 2 : 1;
}

/* Accent bleeds coral -> red as the session gauge nears its cap — a
 * consumption meter should visibly tighten, unlike a neutral countdown. */
static uint32_t _session_accent(float pct)
{
    if (pct < 80.0f) return CLAUDE_ACCENT;
    float t = (pct - 80.0f) / 20.0f;
    if (t > 1.0f) t = 1.0f;
    return CYBER::blend(CYBER::RED, CLAUDE_ACCENT, t);
}

static uint32_t _weekly_accent()
{
    return CYBER::blend(CYBER::GREEN, CLAUDE_ACCENT, 0.5f);
}

/* Claude's own six-point asterisk (matches the launcher icon), used as the
 * ring's leading-edge spark and the no-data panel's icon. */
static void _asterisk_mark(LGFX_Sprite* c, int x, int y, int r, uint32_t col)
{
    for (int i = 0; i < 6; i++)
    {
        float th = i * 60.0f * 0.01745329f;
        int dx = (int)(r * cosf(th));
        int dy = (int)(r * sinf(th));
        c->drawLine(x - dx, y - dy, x + dx, y + dy, col);
    }
}

/* "2h 14m" / "3d 5h" / "45m" — a relative countdown reads at a glance,
 * unlike the sender's absolute "Resets Oct 24, 3pm (PDT)" string. */
static void _fmt_mins(char* dst, size_t n, int mins)
{
    if (mins < 0) mins = 0;
    int d = mins / 1440, h = (mins % 1440) / 60, m = mins % 60;
    if (d > 0)      snprintf(dst, n, "%dd %dh", d, h);
    else if (h > 0) snprintf(dst, n, "%dh %02dm", h, m);
    else            snprintf(dst, n, "%dm", m);
}

/* Horizontal meter for the weekly figure: dim track, accent fill, bright
 * leading tick. Pill-shaped so it sits comfortably inside the round bezel. */
static void _draw_bar(LGFX_Sprite* c, int cx, int y, int w, int h, float frac, uint32_t accent)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int x = cx - w / 2;
    c->fillRoundRect(x, y, w, h, h / 2, CYBER::blend(CYBER::TRACK, CYBER::BG, 0.9f));
    int fw = (int)(w * frac);
    if (fw >= h)
    {
        c->fillRoundRect(x, y, fw, h, h / 2, accent);
        c->fillRect(x + fw - 2, y, 2, h, CYBER::WHITE);
    }
    else if (fw > 0)
        c->fillCircle(x + h / 2, y + h / 2, h / 2, accent);
}

/* Hero gauge: the established segmented pip-ring (progressRingAt), plus
 * Claude's asterisk spark at the leading edge. */
static void _draw_reactor_ring(LGFX_Sprite* c, float sessionFrac, uint32_t accent)
{
    CYBER::progressRingAt(c, RING_CX, RING_CY, RING_R, sessionFrac, accent);

    if (sessionFrac > 0.002f)
    {
        float endTh = (-90.0f + 360.0f * sessionFrac) * 0.01745329f;
        int ex = RING_CX + (int)(RING_R * cosf(endTh));
        int ey = RING_CY + (int)(RING_R * sinf(endTh));
        _asterisk_mark(c, ex, ey, 6, CYBER::WHITE);
    }
}

static void _draw_no_data_panel(LGFX_Sprite* c, uint32_t accent)
{
    _asterisk_mark(c, 120, 62, 16, accent);

    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font4);
    c->setTextColor(CYBER::WHITE);
    c->drawString("Claude", 120, 100);

    c->setFont(&fonts::Font2);
    c->setTextColor(CYBER::blend(accent, CYBER::WHITE, 0.4f));
    c->drawString("waiting for data...", 120, 128);

    c->drawFastHLine(60, 146, 120, CYBER::blend(accent, CYBER::BG, 0.35f));

    c->setTextColor(CYBER::SLATE);
    c->drawString("BLE  M5DIAL-001", 120, 162);
    c->drawString("or USB: claude_sender.py", 120, 180);

    c->setFont(&fonts::Font0);
    c->setTextColor(CYBER::DIMTEXT);
    c->drawString(PC_LINK::bleAddress(), 120, 198);
}

void ClaudeUsage::_render()
{
    LGFX_Sprite* c = _data.hal->canvas;
    c->fillScreen(CYBER::BG);

    auto& u = PC_LINK::claudeUsage;

    if (!u.valid)
    {
        _draw_no_data_panel(c, CLAUDE_ACCENT);
        _canvas_update();
        return;
    }

    uint32_t accent = _session_accent(u.session_pct);
    _draw_reactor_ring(c, u.session_pct / 100.0f, accent);

    /* Header: "5H SESSION" normally; replaced by the status in its warning
     * colour when the API reports we're limited or rejected. */
    bool limited  = (strncmp(u.status, "limited",  7) == 0);
    bool rejected = (strncmp(u.status, "rejected", 8) == 0);
    c->setTextDatum(textdatum_t::middle_center);
    c->setFont(&fonts::Font2);
    if (limited || rejected)
    {
        c->setTextColor(limited ? CYBER::AMBER : CYBER::RED);
        c->drawString(limited ? "RATE LIMITED" : "REJECTED", RING_CX, 42);
    }
    else
    {
        c->setTextColor(CYBER::blend(accent, CYBER::WHITE, 0.55f));
        c->drawString("5H SESSION", RING_CX, 42);
    }

    /* Hero number + "%" set together and centred as one unit. No chromatic
     * shadow here — it smeared the digits at this size. */
    char big[8];
    snprintf(big, sizeof(big), "%d", (int)(u.session_pct + 0.5f));
    c->setFont(&fonts::Font7);
    int nw = c->textWidth(big);
    c->setFont(&fonts::Font4);
    int pw = c->textWidth("%");
    int x0 = RING_CX - (nw + 4 + pw) / 2;

    c->setFont(&fonts::Font7);
    c->setTextDatum(textdatum_t::middle_left);
    c->setTextColor(u.session_pct >= 80.0f ? accent : CYBER::WHITE);
    c->drawString(big, x0, 90);
    c->setFont(&fonts::Font4);
    c->setTextDatum(textdatum_t::bottom_left);
    c->setTextColor(accent);
    c->drawString("%", x0 + nw + 4, 112);

    char rs[24], line[32];
    _fmt_mins(rs, sizeof(rs), u.session_reset_mins);
    snprintf(line, sizeof(line), "resets in %s", rs);
    c->setFont(&fonts::Font2);
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(CYBER::SLATE);
    c->drawString(line, RING_CX, 132);

    c->drawFastHLine(RING_CX - 60, 151, 120, CYBER::blend(CYBER::BORDER, CYBER::BG, 0.8f));

    /* Weekly block: label left, value right, meter, then its reset. */
    uint32_t wAccent = _weekly_accent();
    const int WX = RING_CX - 60, WW = 120;
    c->setFont(&fonts::Font2);
    c->setTextDatum(textdatum_t::middle_left);
    c->setTextColor(CYBER::SLATE);
    c->drawString("WEEK", WX, 166);
    char wp[8];
    snprintf(wp, sizeof(wp), "%d%%", (int)(u.weekly_pct + 0.5f));
    c->setTextDatum(textdatum_t::middle_right);
    c->setTextColor(CYBER::WHITE);
    c->drawString(wp, WX + WW, 166);

    _draw_bar(c, RING_CX, 177, WW, 6, u.weekly_pct / 100.0f, wAccent);

    _fmt_mins(rs, sizeof(rs), u.weekly_reset_mins);
    snprintf(line, sizeof(line), "resets in %s", rs);
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(CYBER::blend(CYBER::SLATE, CYBER::BG, 0.75f));
    c->drawString(line, RING_CX, 193);

    _canvas_update();
}

void ClaudeUsage::onSetup()
{
    setAppName("ClaudeUsage");
    setAllowBgRunning(false);

    CLAUDE_USAGE::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}

void ClaudeUsage::onCreate()
{
    _log("onCreate");

    /* Reset stale data from a previous session. */
    PC_LINK::claudeUsage.valid = false;
    PC_LINK::claudeUsageVersion++;

    PC_LINK::setActive(true);

    _data.drawn_version = PC_LINK::claudeUsageVersion;
    _data.drawn_valid   = false;
    _data.last_data_ms  = 0;

    _render();
}

void ClaudeUsage::onRunning()
{
    PC_LINK::poll();

    if (PC_LINK::claudeUsage.valid && PC_LINK::claudeUsageVersion != _data.drawn_version)
        _data.last_data_ms = millis();

    /* Staleness: revert to the no-data panel if the stream stops for >90s
     * (claude_sender.py polls every 60s, so 90s gives a 1.5x buffer). */
    if (PC_LINK::claudeUsage.valid && _data.last_data_ms &&
        millis() - _data.last_data_ms > 90000)
    {
        PC_LINK::claudeUsage.valid = false;
        PC_LINK::claudeUsageVersion++;
    }

    if (PC_LINK::claudeUsageVersion != _data.drawn_version)
    {
        _data.drawn_version = PC_LINK::claudeUsageVersion;
        _data.drawn_valid   = PC_LINK::claudeUsage.valid;
        _render();
    }

    if (_read_button(_data.hal) == 2)
    {
        destroyApp();
        return;
    }

    delay(20);
}

void ClaudeUsage::onDestroy()
{
    _log("onDestroy");
    PC_LINK::setActive(false);
}
