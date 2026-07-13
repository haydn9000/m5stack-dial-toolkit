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

/* Status is secondary to the hero number — allowed, limited, and rejected
 * all get the same small quiet outline badge so none of them compete with
 * the number for attention. Urgency still reads through colour (and, for
 * session usage, the ring's own coral -> red bleed). */
static void _quiet_pill(LGFX_Sprite* c, int cx, int y, const char* label, uint32_t accent)
{
    c->setFont(&fonts::Font0);
    c->setTextSize(1);
    int tw = c->textWidth(label);
    int w = tw + 22, h = 16, x = cx - w / 2;

    c->drawRoundRect(x, y, w, h, h / 2, CYBER::blend(accent, CYBER::BG, 0.55f));
    c->fillCircle(x + 11, y + h / 2, 2, CYBER::blend(accent, CYBER::BG, 0.7f));
    c->setTextDatum(textdatum_t::middle_left);
    c->setTextColor(CYBER::blend(accent, CYBER::WHITE, 0.15f));
    c->drawString(label, x + 18, y + h / 2);
}

/* Hero gauge: the established segmented pip-ring (progressRingAt), plus
 * Claude's asterisk spark at the leading edge — a small brand touch that's
 * just straight lines, not text, so it renders identically to the design
 * mockup (unlike rotated rim text, which doesn't translate to the real
 * bitmap font — see the "prior design" revert this replaced). */
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
    int x = 24, y = 78, w = 192, h = 104;
    c->fillRoundRect(x, y, w, h, 10, CYBER::blend(CYBER::BG, CYBER::WHITE, 0.06f));
    c->drawRoundRect(x, y, w, h, 10, CYBER::blend(accent, CYBER::BG, 0.45f));

    _asterisk_mark(c, 120, 100, 14, CYBER::blend(accent, CYBER::BG, 0.85f));

    c->setFont(&fonts::Font0);
    c->setTextSize(1);
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(CYBER::WHITE);
    c->drawString("AWAITING DATA", 120, 122);

    c->setTextColor(CYBER::SLATE);
    c->drawString("BLE   M5DIAL-001", 120, 142);
    c->drawString("USB   claude_sender.py <port>", 120, 158);

    char addr[48];
    snprintf(addr, sizeof(addr), "ADDR  %s", PC_LINK::bleAddress());
    c->setTextColor(CYBER::blend(CYBER::SLATE, CYBER::BG, 0.8f));
    c->drawString(addr, 120, 174);
}

void ClaudeUsage::_render()
{
    LGFX_Sprite* c = _data.hal->canvas;
    c->fillScreen(CYBER::BG);
    for (int y = 0; y < 240; y += 4) c->drawFastHLine(0, y, 240, CYBER::SCANLINE);

    auto& u = PC_LINK::claudeUsage;

    if (!u.valid)
    {
        c->setFont(&fonts::Font0);
        c->setTextSize(2);
        c->setTextDatum(textdatum_t::middle_center);
        c->setTextColor(CLAUDE_ACCENT);
        c->drawString("CLAUDE USAGE", RING_CX, 42);

        _draw_no_data_panel(c, CLAUDE_ACCENT);
        _canvas_update();
        return;
    }

    uint32_t accent = _session_accent(u.session_pct);
    float sessionFrac = u.session_pct / 100.0f;

    _draw_reactor_ring(c, sessionFrac, accent);

    bool limited  = (strncmp(u.status, "limited",  7) == 0);
    bool rejected = (strncmp(u.status, "rejected", 8) == 0);
    const char* statusText = limited ? "LIMITED" : (rejected ? "REJECTED" : "ALLOWED");
    uint32_t statusAccent  = limited ? CYBER::AMBER : (rejected ? CYBER::RED : CYBER::GREEN);
    _quiet_pill(c, RING_CX, 48, statusText, statusAccent);

    char big[4];
    snprintf(big, sizeof(big), "%2d", (int)u.session_pct);
    CYBER::bigTime(c, big, CYBER::WHITE, 0.0f);

    c->setFont(&fonts::Font0);
    c->setTextSize(1);
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(CYBER::blend(accent, CYBER::WHITE, 0.3f));
    c->drawString("CURRENT", RING_CX, 150);

    c->setTextColor(CYBER::SLATE);
    c->drawString(u.session_reset_str[0] ? u.session_reset_str : "", RING_CX, 167);

    char weeklyStr[24];
    snprintf(weeklyStr, sizeof(weeklyStr), "WEEKLY 7D  %d%%", (int)u.weekly_pct);
    c->setTextColor(_weekly_accent());
    c->drawString(weeklyStr, RING_CX, 184);

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
