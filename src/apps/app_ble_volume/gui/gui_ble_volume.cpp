/**
 * @file gui_ble_volume.cpp
 * @brief Cyberpunk volume HUD: dark terminal backdrop with scanlines, neon
 *        segmented gauge ring, targeting-bracket HUD frame, glitchy 7-segment
 *        readout, and live link telemetry.
 */
#include "gui_ble_volume.h"
#include <cmath>

// delay() and millis() are provided by the Arduino framework

/* ---- Neon palette (RGB888) ---- */
namespace
{
    constexpr uint32_t COL_BG_TOP   = 0x0A1622; /* dark cool black (neon) */
    constexpr uint32_t COL_BG_BOT   = 0x02060C; /* near-black      */
    constexpr uint32_t COL_CYAN     = 0x00F0FF; /* electric cyan   */
    constexpr uint32_t COL_MAGENTA  = 0xFF2A6D; /* hot pink-magenta*/
    constexpr uint32_t COL_PINK     = 0xFF6EC7; /* soft pink       */
    constexpr uint32_t COL_SCAN     = 0x0C1C28; /* scanline tint   */
    constexpr uint32_t COL_DIM      = 0x123642; /* unlit tick      */
    constexpr uint32_t COL_AMBER    = 0xFCEE0A; /* signature yellow*/
    constexpr uint32_t COL_GREEN    = 0x00FF9F; /* online          */
    constexpr uint32_t COL_RED      = 0xFF003C; /* mute / alert    */

    constexpr float DEG2RAD = 0.0174532925f;

    /* Linear interpolate between two RGB888 colors */
    uint32_t lerp_color(uint32_t a, uint32_t b, float t)
    {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
        int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
        int r = ar + (int)((br - ar) * t);
        int g = ag + (int)((bg - ag) * t);
        int bl = ab + (int)((bb - ab) * t);
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
    }
}


void GUI_BLE_Volume::_draw_background()
{
    /* Deep terminal gradient */
    for (int y = 0; y < 240; y += 2)
    {
        uint32_t col = lerp_color(COL_BG_TOP, COL_BG_BOT, y / 239.0f);
        _canvas->fillRect(0, y, 240, 2, col);
    }
}


void GUI_BLE_Volume::_draw_scanlines()
{
    /* CRT scanline overlay every 3px */
    for (int y = 0; y < 240; y += 3)
        _canvas->drawFastHLine(0, y, 240, COL_SCAN);
}


void GUI_BLE_Volume::_draw_hud_frame(bool muted)
{
    const uint32_t edge = muted ? COL_RED : COL_CYAN;
    const uint32_t glow = lerp_color(edge, COL_BG_BOT, 0.55f);

    /* Targeting brackets at NE / NW / SE / SW (45/135/225/315 deg) */
    const int cx = 120, cy = 120, r = 113, len = 16;
    const float angs[4] = {45.0f, 135.0f, 225.0f, 315.0f};
    for (int k = 0; k < 4; k++)
    {
        float a = angs[k] * DEG2RAD;
        int bx = cx + (int)(r * cosf(a));
        int by = cy + (int)(r * sinf(a));
        /* Tangential tick */
        float tx = -sinf(a), ty = cosf(a);
        _canvas->drawLine(bx - (int)(tx * len), by - (int)(ty * len),
                          bx + (int)(tx * len), by + (int)(ty * len), edge);
        /* Inward stub */
        _canvas->drawLine(bx, by, cx + (int)((r - 10) * cosf(a)),
                          cy + (int)((r - 10) * sinf(a)), glow);
    }

    /* Side telemetry ticks (left & right rails) */
    for (int i = 0; i < 5; i++)
    {
        int yy = 96 + i * 12;
        _canvas->drawFastHLine(16, yy, 6, glow);
        _canvas->drawFastHLine(218, yy, 6, glow);
    }

    /* Lower data strip */
    _canvas->setFont(GUI_FONT_CN_SMALL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(muted ? COL_RED : COL_CYAN);
    _canvas->drawCenterString("// AUDIO.LINK", 120, 168);
}


void GUI_BLE_Volume::_draw_gauge(int volume, bool muted)
{
    /* Segmented neon ring: 270deg sweep, gap centred at the bottom.
     * Index 0 = bottom-left, increasing clockwise to bottom-right. */
    const int   N      = 40;
    const int   cx     = 120, cy = 120, radius = 104;
    const float start  = 135.0f;   /* bottom-left in screen degrees */
    const float sweep  = 270.0f;

    int active = (int)lroundf(volume / 100.0f * N);

    for (int i = 0; i < N; i++)
    {
        float theta = (start + sweep * i / (N - 1)) * DEG2RAD;
        float c = cosf(theta), s = sinf(theta);
        int x = cx + (int)(radius * c);
        int y = cy + (int)(radius * s);

        /* Quarter scale ticks sit just outside the ring */
        bool major = (i % 10 == 0) || (i == N - 1);
        if (major)
        {
            int mx = cx + (int)((radius + 9) * c);
            int my = cy + (int)((radius + 9) * s);
            _canvas->drawLine(x, y, mx, my, muted ? COL_RED : COL_DIM);
        }

        if (i < active)
        {
            /* Neon gradient cyan -> magenta -> pink along the ring */
            float t = (float)i / (N - 1);
            uint32_t col = muted
                ? COL_RED
                : (t < 0.5f ? lerp_color(COL_CYAN, COL_MAGENTA, t * 2.0f)
                            : lerp_color(COL_MAGENTA, COL_PINK, (t - 0.5f) * 2.0f));
            /* Glow halo + bright core, drawn as little bars (techy) */
            _canvas->fillCircle(x, y, 4, lerp_color(col, COL_BG_BOT, 0.6f));
            _canvas->fillCircle(x, y, 2, col);
        }
        else
        {
            _canvas->fillCircle(x, y, 1, COL_DIM);
        }
    }

    /* Bright "head" marker at the current level */
    if (active > 0 && active <= N)
    {
        int idx = active - 1;
        float theta = (start + sweep * idx / (N - 1)) * DEG2RAD;
        int x = cx + (int)(radius * cosf(theta));
        int y = cy + (int)(radius * sinf(theta));
        _canvas->fillCircle(x, y, 5, muted ? COL_RED : COL_PINK);
        _canvas->fillCircle(x, y, 2, TFT_WHITE);
    }
}


void GUI_BLE_Volume::_draw_status(bool connected)
{
    _canvas->setFont(GUI_FONT_CN_SMALL);
    _canvas->setTextSize(1);

    if (connected)
    {
        _canvas->setTextColor(COL_GREEN);
        _canvas->fillCircle(92, 56, 3, COL_GREEN);
        _canvas->drawCenterString("LINK//OK", 124, 48);
    }
    else
    {
        _canvas->setTextColor(COL_AMBER);
        _canvas->drawCenterString("SCANNING", 120, 48);
    }
}


void GUI_BLE_Volume::_draw_center_number(int volume, bool muted)
{
    if (muted)
    {
        /* 7-segment digital readout (still shows the level, e.g. 0) */
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", volume);
        _canvas->setFont(&fonts::Font7);
        _canvas->setTextSize(1);
        _canvas->setTextColor(lerp_color(COL_RED, COL_BG_BOT, 0.45f));
        _canvas->drawCenterString(buf, 121, 70);
        _canvas->setTextColor(COL_RED);
        _canvas->drawCenterString(buf, 120, 70);

        _canvas->setFont(GUI_FONT_CN_BIG);
        _canvas->setTextSize(1);
        /* Glitch shadow then bright */
        _canvas->setTextColor(lerp_color(COL_RED, COL_BG_BOT, 0.4f));
        _canvas->drawCenterString("[MUTE]", 122, 139);
        _canvas->setTextColor(COL_RED);
        _canvas->drawCenterString("[MUTE]", 120, 138);
        return;
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", volume);

    /* Occasional horizontal "glitch" offset for a cyberpunk flicker */
    int gx = ((millis() / 90) % 37 == 0) ? 2 : 0;

    /* 7-segment digital readout with chromatic-aberration glow */
    _canvas->setFont(&fonts::Font7);
    _canvas->setTextSize(1);

    _canvas->setTextColor(lerp_color(COL_MAGENTA, COL_BG_BOT, 0.45f));
    _canvas->drawCenterString(buf, 122 + gx, 74);
    _canvas->setTextColor(lerp_color(COL_CYAN, COL_BG_BOT, 0.45f));
    _canvas->drawCenterString(buf, 118 - gx, 74);
    _canvas->setTextColor(COL_CYAN);
    _canvas->drawCenterString(buf, 120, 74);
}


void GUI_BLE_Volume::renderPage(bool connected, int volume, bool muted)
{
    _draw_background();
    _draw_hud_frame(muted);
    _draw_gauge(volume, muted);
    _draw_status(connected);
    _draw_center_number(volume, muted);
    _draw_scanlines();

    /* Quit hint */
    _canvas->setFont(GUI_FONT_CN_BIG);
    _draw_quit_button(COL_CYAN);

    /* Neon outer rim */
    _canvas->drawCircle(120, 120, 119, muted ? COL_RED : COL_CYAN);
    _canvas->drawCircle(120, 120, 118, lerp_color(muted ? COL_RED : COL_CYAN, COL_BG_BOT, 0.45f));

    _canvas->pushSprite(0, 0);
}


void GUI_BLE_Volume::init()
{
    /* Draw the full frame once at open */
    renderPage(false, 50, false);
}


