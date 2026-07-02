/**
 * @file gui_set_brightness.cpp
 * @brief Cyberpunk brightness HUD: dark terminal backdrop with scanlines, an
 *        amber neon segmented gauge ring, targeting-bracket HUD frame, a
 *        reactive luminance core, and a glitchy 7-segment percentage readout.
 */
#include "gui_set_brightness.h"
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


void GUI_SetBrightness::_draw_background(uint8_t brightness)
{
    /* Backdrop warms slightly as brightness rises */
    float lvl = brightness / 255.0f;
    uint32_t top = lerp_color(COL_BG_TOP, lerp_color(COL_BG_TOP, COL_ORANGE, 0.35f), lvl);
    for (int y = 0; y < 240; y += 2)
    {
        uint32_t col = lerp_color(top, COL_BG_BOT, y / 239.0f);
        _canvas->fillRect(0, y, 240, 2, col);
    }
}


void GUI_SetBrightness::_draw_scanlines()
{
    for (int y = 0; y < 240; y += 3)
        _canvas->drawFastHLine(0, y, 240, COL_SCAN);
}


void GUI_SetBrightness::_draw_hud_frame(uint8_t brightness)
{
    const uint32_t edge = COL_AMBER;
    const uint32_t glow = lerp_color(edge, COL_BG_BOT, 0.55f);

    /* Targeting brackets at the diagonals */
    const int cx = 120, cy = 120, r = 113, len = 16;
    const float angs[4] = {45.0f, 135.0f, 225.0f, 315.0f};
    for (int k = 0; k < 4; k++)
    {
        float a = angs[k] * DEG2RAD;
        int bx = cx + (int)(r * cosf(a));
        int by = cy + (int)(r * sinf(a));
        float tx = -sinf(a), ty = cosf(a);
        _canvas->drawLine(bx - (int)(tx * len), by - (int)(ty * len),
                          bx + (int)(tx * len), by + (int)(ty * len), edge);
        _canvas->drawLine(bx, by, cx + (int)((r - 10) * cosf(a)),
                          cy + (int)((r - 10) * sinf(a)), glow);
    }

    /* Side telemetry ticks */
    for (int i = 0; i < 5; i++)
    {
        int yy = 96 + i * 12;
        _canvas->drawFastHLine(16, yy, 6, glow);
        _canvas->drawFastHLine(218, yy, 6, glow);
    }

    /* --- Luminance core: a reactor whose glow tracks the level --- */
    float lvl = brightness / 255.0f;
    const int coreCx = 120, coreCy = 158;
    int halo = (int)(10 + 16 * lvl);
    _canvas->fillCircle(coreCx, coreCy, halo, lerp_color(COL_BG_BOT, COL_ORANGE, 0.18f + 0.4f * lvl));
    _canvas->fillCircle(coreCx, coreCy, 9, lerp_color(COL_ORANGE, COL_YELLOW, lvl));
    _canvas->fillCircle(coreCx, coreCy, 4, lerp_color(COL_YELLOW, TFT_WHITE, lvl));
    /* Radiating energy spokes */
    for (int k = 0; k < 8; k++)
    {
        float a = (k * 45.0f + (millis() / 24) % 360) * DEG2RAD;
        int x0 = coreCx + (int)(11 * cosf(a)), y0 = coreCy + (int)(11 * sinf(a));
        int x1 = coreCx + (int)((11 + 6 * lvl) * cosf(a)), y1 = coreCy + (int)((11 + 6 * lvl) * sinf(a));
        _canvas->drawLine(x0, y0, x1, y1, lerp_color(COL_AMBER, COL_BG_BOT, 0.3f));
    }

    /* Lower data strip */
    _canvas->setFont(GUI_FONT_CN_SMALL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COL_AMBER);
    _canvas->drawCenterString("// LUMEN.CTRL", 120, 196);
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

    int gx = ((millis() / 90) % 37 == 0) ? 2 : 0;

    /* 7-segment readout with chromatic-aberration glow */
    _canvas->setFont(&fonts::Font7);
    _canvas->setTextSize(1);

    _canvas->setTextColor(lerp_color(COL_MAGENTA, COL_BG_BOT, 0.45f));
    _canvas->drawCenterString(buf, 122 + gx, 74);
    _canvas->setTextColor(lerp_color(COL_CYAN, COL_BG_BOT, 0.5f));
    _canvas->drawCenterString(buf, 118 - gx, 74);
    _canvas->setTextColor(COL_YELLOW);
    _canvas->drawCenterString(buf, 120, 74);

    /* Label */
    _canvas->setFont(GUI_FONT_CN_SMALL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COL_AMBER);
    _canvas->drawCenterString("LUMEN %", 120, 50);
}


void GUI_SetBrightness::renderPage(uint8_t brightness)
{
    _draw_background(brightness);
    _draw_hud_frame(brightness);
    _draw_gauge(brightness);
    _draw_center_number(brightness);
    _draw_scanlines();

    /* Quit hint */
    _canvas->setFont(GUI_FONT_CN_BIG);
    _draw_quit_button(COL_AMBER);

    /* Neon outer rim */
    _canvas->drawCircle(120, 120, 119, COL_AMBER);
    _canvas->drawCircle(120, 120, 118, lerp_color(COL_AMBER, COL_BG_BOT, 0.45f));

    _canvas->pushSprite(0, 0);
}


void GUI_SetBrightness::init()
{
}
