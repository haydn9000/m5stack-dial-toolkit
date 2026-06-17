/**
 * @file cyber_ui.hpp
 * @brief Shared cyberpunk-styled drawing helpers for the time apps
 *        (stopwatch / timer / pomodoro).
 *
 * Header-only. Only depends on LovyanGFX. All drawing targets an
 * LGFX_Sprite canvas (the 240x240 round display). Colours are 24-bit RGB
 * (LovyanGFX accepts these directly and converts to RGB565).
 */
#pragma once
#include <LovyanGFX.hpp>
#include <math.h>
#include <string.h>


namespace CYBER
{
    /* --- Neon palette (muted: cyberpunk hues, easier on the eyes) --- */
    constexpr uint32_t BG       = 0x05070D;  // near-black blue
    constexpr uint32_t SCANLINE = 0x0A0F18;  // faint CRT scanline
    constexpr uint32_t TRACK    = 0x132030;  // dim ring track
    constexpr uint32_t BORDER   = 0x24384C;  // ring edge line
    constexpr uint32_t CYAN     = 0x33B5C4;  // muted cyan
    constexpr uint32_t MAGENTA  = 0xC2557E;  // dusty magenta
    constexpr uint32_t AMBER    = 0xD79A3A;  // muted amber
    constexpr uint32_t GREEN    = 0x4FBE8B;  // muted mint
    constexpr uint32_t RED      = 0xCE5060;  // muted red
    constexpr uint32_t WHITE    = 0xDCEAF0;  // soft off-white
    constexpr uint32_t DIMTEXT  = 0x5A7488;

    /* Geometry of the progress ring on the round display */
    constexpr int CX    = 120;
    constexpr int CY    = 120;
    constexpr int R_OUT = 117;
    constexpr int R_IN  = 100;

    /* Linear blend fg over bg, a in [0,1]. */
    inline uint32_t blend(uint32_t fg, uint32_t bg, float a)
    {
        int fr = (fg >> 16) & 0xFF, fg_ = (fg >> 8) & 0xFF, fb = fg & 0xFF;
        int br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
        int r = (int)(fr * a + br * (1 - a));
        int g = (int)(fg_ * a + bg_ * (1 - a));
        int b = (int)(fb * a + bb * (1 - a));
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    /* Draw the HUD targeting frame: diagonal corner brackets, side telemetry
     * rails and a neon outer rim — the cyberpunk "scope" overlay shared with
     * the volume / brightness apps. */
    inline void hudFrame(LGFX_Sprite* c, uint32_t accent)
    {
        const uint32_t glow = blend(accent, BG, 0.45f);

        /* Targeting brackets at the four diagonals (45/135/225/315 deg) */
        const int r = 113, len = 15;
        const float angs[4] = {45.0f, 135.0f, 225.0f, 315.0f};
        for (int k = 0; k < 4; k++)
        {
            float a  = angs[k] * 0.01745329f;
            int bx = CX + (int)(r * cosf(a));
            int by = CY + (int)(r * sinf(a));
            float tx = -sinf(a), ty = cosf(a);           // tangential bracket
            c->drawLine(bx - (int)(tx * len), by - (int)(ty * len),
                        bx + (int)(tx * len), by + (int)(ty * len), accent);
            c->drawLine(bx, by, CX + (int)((r - 9) * cosf(a)),
                        CY + (int)((r - 9) * sinf(a)), glow);
        }

        /* Side telemetry ticks on the left & right rails */
        for (int i = 0; i < 5; i++)
        {
            int yy = 98 + i * 11;
            c->drawFastHLine(15, yy, 6, glow);
            c->drawFastHLine(219, yy, 6, glow);
        }

        /* Neon outer rim */
        c->drawCircle(CX, CY, 119, accent);
        c->drawCircle(CX, CY, 118, glow);
    }

    /* Draw the dark HUD background: gradient fill, CRT scanlines, ring track
     * and the targeting frame tinted with the supplied accent colour. */
    inline void background(LGFX_Sprite* c, uint32_t accent = CYAN)
    {
        /* Subtle vertical gradient: faint accent tint at top -> black bottom */
        uint32_t top = blend(accent, BG, 0.10f);
        for (int y = 0; y < 240; y += 2)
            c->fillRect(0, y, 240, 2, blend(top, BG, y / 239.0f));

        for (int y = 0; y < 240; y += 4)
            c->drawFastHLine(0, y, 240, SCANLINE);

        /* Ring track + edge lines */
        c->fillArc(CX, CY, R_OUT, R_IN, 0, 360, TRACK);
        c->drawCircle(CX, CY, R_OUT, BORDER);
        c->drawCircle(CX, CY, R_IN,  BORDER);

        hudFrame(c, accent);
    }

    /* Draw the neon progress ring as a segmented gauge: lit segments sweep from
     * the top (12 o'clock) clockwise for fraction p, with a glowing head. */
    inline void progressRing(LGFX_Sprite* c, float p, uint32_t col)
    {
        if (p < 0) p = 0;
        if (p > 1) p = 1;

        const int   N      = 48;
        const float radius = (R_OUT + R_IN) / 2.0f;
        int active = (int)lroundf(p * N);

        for (int i = 0; i < N; i++)
        {
            float theta = (-90.0f + 360.0f * i / N) * 0.01745329f;  // top, CW
            float cx = cosf(theta), sy = sinf(theta);
            int x = CX + (int)(radius * cx);
            int y = CY + (int)(radius * sy);

            if (i < active)
            {
                /* Hot core brightens toward the leading head */
                float t   = active > 1 ? (float)i / (active - 1) : 1.0f;
                uint32_t lit = blend(WHITE, col, 0.20f + 0.55f * t);
                c->fillCircle(x, y, 4, blend(col, BG, 0.55f));   // glow halo
                c->fillCircle(x, y, 2, lit);                      // bright core
            }
            else
            {
                c->fillCircle(x, y, 1, blend(col, BG, 0.20f));   // unlit pip
            }
        }

        /* Bright head marker at the leading segment */
        if (active > 0)
        {
            int idx = active - 1;
            float theta = (-90.0f + 360.0f * idx / N) * 0.01745329f;
            int x = CX + (int)(radius * cosf(theta));
            int y = CY + (int)(radius * sinf(theta));
            c->fillSmoothCircle(x, y, 4, col);
            c->fillSmoothCircle(x, y, 2, WHITE);
        }
        else
        {
            /* Start cap at the top when empty */
            c->fillSmoothCircle(CX, CY - (int)radius, 3, blend(WHITE, BG, 0.6f));
        }
    }

    /* Title text near the top, inside the ring. */
    inline void title(LGFX_Sprite* c, const char* t, uint32_t col)
    {
        c->setFont(&fonts::Font0);
        c->setTextSize(2);
        c->setTextColor(col);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(t, CX, 62);
    }

    /* Small subtitle just under the title. */
    inline void subtitle(LGFX_Sprite* c, const char* t, uint32_t col)
    {
        c->setFont(&fonts::Font0);
        c->setTextSize(1);
        c->setTextColor(col);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(t, CX, 84);
    }

    /* Big 7-segment time readout in the centre, with a cyan/magenta
     * chromatic-aberration glow for the HUD look. */
    inline void bigTime(LGFX_Sprite* c, const char* t, uint32_t col)
    {
        c->setFont(&fonts::Font7);
        c->setTextSize(1);
        c->setTextDatum(textdatum_t::middle_center);

        /* Chromatic split: magenta to the right, cyan to the left */
        c->setTextColor(blend(MAGENTA, BG, 0.55f));
        c->drawString(t, CX + 2, 118);
        c->setTextColor(blend(CYAN, BG, 0.55f));
        c->drawString(t, CX - 2, 118);
        /* Bright core */
        c->setTextColor(col);
        c->drawString(t, CX, 118);
    }

    /* Small monospace text under the big readout (e.g. centiseconds). */
    inline void smallReadout(LGFX_Sprite* c, const char* t, uint32_t col, int y = 160)
    {
        c->setFont(&fonts::Font0);
        c->setTextSize(2);
        c->setTextColor(col);
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(t, CX, y);
    }

    /* Bottom hint line, inside the ring. A hint may pack two actions separated
     * by a run of spaces (e.g. "PRESS = PAUSE   HOLD = EXIT"); in that case it
     * is split onto two stacked lines so it stays clear of the ring. */
    inline void hint(LGFX_Sprite* c, const char* t, uint32_t col = DIMTEXT)
    {
        c->setFont(&fonts::Font0);
        c->setTextSize(1);
        c->setTextColor(col);
        c->setTextDatum(textdatum_t::middle_center);

        const char* sep = strstr(t, "  ");   // run of 2+ spaces = action divider
        if (sep == nullptr)
        {
            c->drawString(t, CX, 188);
            return;
        }

        char a[48];
        size_t na = (size_t)(sep - t);
        if (na >= sizeof(a)) na = sizeof(a) - 1;
        memcpy(a, t, na);
        a[na] = '\0';

        const char* p = sep;
        while (*p == ' ') p++;

        c->drawString(a, CX, 184);
        c->drawString(p, CX, 200);
    }
}
