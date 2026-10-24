/**
 * @file gui_ble_volume.cpp
 * @brief Cyberpunk volume HUD: neon segmented 270-degree gauge, the level
 *        in 7-segment digits, and the link state as a plain-language line.
 */
#include "gui_ble_volume.h"
#include "../../utilities/cyber_ui/cyber_ui.hpp"
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
    CYBER::text(_canvas, "Volume", 120, 50, CYBER::FONT_LABEL(), lerp_color(COL_CYAN, CYBER::WHITE, 0.75f));
    if (connected)
        CYBER::chip(_canvas, "Connected", COL_GREEN);
    else
        CYBER::chip(_canvas, "Waiting for computer", COL_AMBER, true);
}


void GUI_BLE_Volume::_draw_center_number(int volume, bool muted)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", volume);
    CYBER::text(_canvas, buf, 120, 116, &fonts::Font7, muted ? COL_RED : CYBER::WHITE);
    if (muted)
        CYBER::text(_canvas, "Muted", 120, 154, CYBER::FONT_LABEL(), COL_RED);
}


void GUI_BLE_Volume::renderPage(bool connected, int volume, bool muted)
{
    _canvas->fillScreen(CYBER::BG);
    _draw_gauge(volume, muted);
    _draw_status(connected);
    _draw_center_number(volume, muted);
    CYBER::hint(_canvas, muted ? "Press to unmute   Hold to exit"
                               : "Press to mute   Hold to exit");
    _canvas->pushSprite(0, 0);
}


void GUI_BLE_Volume::init()
{
    /* Draw the full frame once at open */
    renderPage(false, 50, false);
}


