/**
 * @file gui_set_brightness.cpp
 * @brief Cyberpunk brightness HUD: a warm segmented 270-degree gauge, the
 *        percentage in 7-segment digits, and a small sun that glows with the
 *        level.
 */
#include "gui_set_brightness.h"
#include "../../utilities/cyber_ui/cyber_ui.hpp"
#include <cmath>


// delay() and millis() are provided by the Arduino framework

/* ---- Cyberpunk amber palette (RGB888) ---- */
namespace
{
    constexpr uint32_t COL_BG_TOP   = 0x1A1606; /* warm dark  (neon) */
    constexpr uint32_t COL_BG_BOT   = 0x070401; /* near-black        */
    constexpr uint32_t COL_AMBER    = 0xFFB000; /* warm amber        */
    constexpr uint32_t COL_YELLOW   = 0xFCEE0A; /* signature yellow  */
    constexpr uint32_t COL_ORANGE   = 0xFF6A1A; /* hot orange        */
    constexpr uint32_t COL_MAGENTA  = 0xFF2A6D; /* hot pink-magenta  */
    constexpr uint32_t COL_CYAN     = 0x00F0FF; /* electric cyan     */
    constexpr uint32_t COL_SCAN     = 0x261A06; /* scanline tint     */
    constexpr uint32_t COL_DIM      = 0x423012; /* unlit tick        */

    constexpr float DEG2RAD = 0.0174532925f;

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


/* Luminance core under the number: a small sun whose glow and rays grow
 * with the level, so the screen itself shows what the dial is doing. */
void GUI_SetBrightness::_draw_sun(uint8_t brightness)
{
    float lvl = brightness / 255.0f;
    const int coreCx = 120, coreCy = 150;
    int halo = (int)(9 + 9 * lvl);
    _canvas->fillCircle(coreCx, coreCy, halo, lerp_color(CYBER::BG, COL_ORANGE, 0.12f + 0.3f * lvl));
    _canvas->fillSmoothCircle(coreCx, coreCy, 6, lerp_color(COL_ORANGE, COL_YELLOW, lvl));
    for (int k = 0; k < 8; k++)
    {
        float a = k * 45.0f * DEG2RAD;
        int r0 = halo + 3, r1 = halo + 3 + (int)(2 + 5 * lvl);
        _canvas->drawLine(coreCx + (int)(r0 * cosf(a)), coreCy + (int)(r0 * sinf(a)),
                          coreCx + (int)(r1 * cosf(a)), coreCy + (int)(r1 * sinf(a)),
                          lerp_color(COL_AMBER, CYBER::BG, 0.2f));
    }
}


void GUI_SetBrightness::_draw_gauge(uint8_t brightness)
{
    const int   N      = 40;
    const int   cx     = 120, cy = 120, radius = 104;
    const float start  = 135.0f;
    const float sweep  = 270.0f;

    int active = (int)lroundf(brightness / 255.0f * N);

    for (int i = 0; i < N; i++)
    {
        float theta = (start + sweep * i / (N - 1)) * DEG2RAD;
        float c = cosf(theta), s = sinf(theta);
        int x = cx + (int)(radius * c);
        int y = cy + (int)(radius * s);

        bool major = (i % 10 == 0) || (i == N - 1);
        if (major)
        {
            int mx = cx + (int)((radius + 9) * c);
            int my = cy + (int)((radius + 9) * s);
            _canvas->drawLine(x, y, mx, my, COL_DIM);
        }

        if (i < active)
        {
            /* Warm gradient: yellow -> amber -> orange/magenta */
            float t = (float)i / (N - 1);
            uint32_t col = (t < 0.5f)
                ? lerp_color(COL_YELLOW, COL_AMBER, t * 2.0f)
                : lerp_color(COL_AMBER, COL_MAGENTA, (t - 0.5f) * 2.0f);
            _canvas->fillCircle(x, y, 4, lerp_color(col, COL_BG_BOT, 0.6f));
            _canvas->fillCircle(x, y, 2, col);
        }
        else
        {
            _canvas->fillCircle(x, y, 1, COL_DIM);
        }
    }

    if (active > 0 && active <= N)
    {
        int idx = active - 1;
        float theta = (start + sweep * idx / (N - 1)) * DEG2RAD;
        int x = cx + (int)(radius * cosf(theta));
        int y = cy + (int)(radius * sinf(theta));
        _canvas->fillCircle(x, y, 5, COL_YELLOW);
        _canvas->fillCircle(x, y, 2, TFT_WHITE);
    }
}


void GUI_SetBrightness::_draw_center_number(uint8_t brightness)
{
    int percent = (brightness * 100 + 127) / 255;

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", percent);

    CYBER::text(_canvas, "Brightness", 120, 50, CYBER::FONT_LABEL(), lerp_color(COL_AMBER, CYBER::WHITE, 0.3f));

    /* Number + "%" centred together as one unit */
    _canvas->setFont(&fonts::Font7);
    int nw = _canvas->textWidth(buf);
    _canvas->setFont(CYBER::FONT_VALUE());
    int pw = _canvas->textWidth("%");
    int x0 = 120 - (nw + 4 + pw) / 2;
    CYBER::text(_canvas, buf, x0, 100, &fonts::Font7, CYBER::WHITE, textdatum_t::middle_left);
    CYBER::text(_canvas, "%", x0 + nw + 4, 122, CYBER::FONT_VALUE(), COL_AMBER, textdatum_t::bottom_left);
}


void GUI_SetBrightness::renderPage(uint8_t brightness)
{
    _canvas->fillScreen(CYBER::BG);
    _draw_gauge(brightness);
    _draw_sun(brightness);
    _draw_center_number(brightness);
    CYBER::hint(_canvas, "Turn to adjust   Press to save");
    _canvas->pushSprite(0, 0);
}


void GUI_SetBrightness::init()
{
}
