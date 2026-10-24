/**
 * @file cyber_ui.hpp
 * @brief Shared cyberpunk-styled drawing helpers for the time apps
 *        (stopwatch / timer / pomodoro / set time) and the monitor apps.
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
    /* --- Neon cyberpunk palette: electric yellow + neon cyan + hot red on
     *     near-black. The AMBER slot carries the signature electric yellow. --- */
    constexpr uint32_t BG       = 0x070B12;  // near-black, cool tint
    constexpr uint32_t SCANLINE = 0x0C1420;  // faint CRT scanline
    constexpr uint32_t TRACK    = 0x13293A;  // dim ring track
    constexpr uint32_t BORDER   = 0x1F4A5E;  // ring edge line
    constexpr uint32_t CYAN     = 0x00F0FF;  // electric cyan
    constexpr uint32_t MAGENTA  = 0xFF2A6D;  // hot pink-magenta
    constexpr uint32_t AMBER    = 0xFCEE0A;  // signature electric yellow
    constexpr uint32_t GOLD     = 0xE8B923;  // burnt-gold / molten yellow — warmer & darker than AMBER
    constexpr uint32_t GREEN    = 0x00FF9F;  // neon mint
    constexpr uint32_t RED      = 0xFF003C;  // alert red
    constexpr uint32_t WHITE    = 0xEAF6FF;  // cool off-white core
    constexpr uint32_t DIMTEXT  = 0x4E6B7C;
    constexpr uint32_t SLATE    = 0x8FB0C2;  // legible secondary text — brighter than DIMTEXT;
                                              // use for labels/readouts that must stay readable,
                                              // not just decorative dimming

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

    /* Type scale for the round 240px panel. Everything that has to be read
     * is DejaVu (a clean sans with real lowercase) at 12/18/24px; Font7's
     * seven-segment digits are kept for the hero readout only. Font0 (6x8)
     * is too small to read at arm's length and is not used for content. */
    inline const lgfx::IFont* FONT_SMALL()  { return &fonts::DejaVu12; }
    inline const lgfx::IFont* FONT_LABEL()  { return &fonts::DejaVu18; }
    inline const lgfx::IFont* FONT_VALUE()  { return &fonts::DejaVu24; }

    inline void text(LGFX_Sprite* c, const char* t, int x, int y, const lgfx::IFont* f,
                     uint32_t col, textdatum_t datum = textdatum_t::middle_center)
    {
        c->setFont(f);
        c->setTextSize(1);
        c->setTextColor(col);
        c->setTextDatum(datum);
        c->drawString(t, x, y);
    }

    /* Plain dark ground. The scanlines, gradient and corner brackets that used
     * to live here competed with the readout, so the ring and the digits now
     * carry the look on their own. `accent` is kept for API compatibility. */
    inline void background(LGFX_Sprite* c, uint32_t accent = CYAN)
    {
        (void)accent;
        c->fillScreen(BG);
    }

    /* Segmented progress ring — 60 crisp pips floating on black, sweeping from
     * the top (12 o'clock) clockwise for fraction p, bright white leading head,
     * plus a faint rim line. Matches design/timeapp-hud.html. */
    inline void progressRing(LGFX_Sprite* c, float p, uint32_t col)
    {
        if (p < 0) p = 0;
        if (p > 1) p = 1;

        const int   N = 60;
        const float R = 108.0f;

        for (int i = 0; i < N; i++)
        {
            float theta = (-90.0f + 360.0f * i / N) * 0.01745329f;  // top, CW
            int x = CX + (int)(R * cosf(theta));
            int y = CY + (int)(R * sinf(theta));

            float d = p * N - i;
            if (d > 0.0f)
            {
                if (d < 1.0f)   c->fillSmoothCircle(x, y, 2, WHITE);            // head
                else            c->fillCircle(x, y, 2, blend(col, BG, 0.9f));   // lit
            }
            else
            {
                c->fillCircle(x, y, 1, blend(BORDER, BG, 0.8f));               // unlit
            }
        }

        c->drawCircle(CX, CY, 116, blend(col, BG, 0.5f));   // rim line
    }

    /* Generalized version of progressRing() — arbitrary center/radius, so
     * callers can draw multiple gauges of different sizes on one canvas
     * (e.g. the Claude Usage app's concentric rings, or PC Stats' clustered
     * small gauges). Pip count auto-scales with radius (~7px between pips)
     * so smaller rings don't look sparse. progressRing() itself is left
     * untouched — it has a different fixed pip count (60) that Timer/
     * Stopwatch/Pomodoro's existing look depends on. */
    inline void progressRingAt(LGFX_Sprite* c, int cx, int cy, int radius, float p, uint32_t col)
    {
        if (p < 0) p = 0;
        if (p > 1) p = 1;

        int N = (int)(2.0f * 3.14159265f * radius / 7.0f);
        if (N < 16) N = 16;

        for (int i = 0; i < N; i++)
        {
            float theta = (-90.0f + 360.0f * i / N) * 0.01745329f;
            int x = cx + (int)(radius * cosf(theta));
            int y = cy + (int)(radius * sinf(theta));

            float d = p * N - i;
            if (d > 0.0f)
            {
                if (d < 1.0f)   c->fillSmoothCircle(x, y, 2, WHITE);
                else            c->fillCircle(x, y, 2, blend(col, BG, 0.9f));
            }
            else
            {
                c->fillCircle(x, y, 1, blend(BORDER, BG, 0.8f));
            }
        }

        c->drawCircle(cx, cy, radius + 4, blend(col, BG, 0.5f));
    }

    /* Title text near the top, inside the ring. */
    inline void title(LGFX_Sprite* c, const char* t, uint32_t col)
    {
        text(c, t, CX, 56, FONT_LABEL(), col);
    }

    /* Small subtitle just under the title. */
    inline void subtitle(LGFX_Sprite* c, const char* t, uint32_t col)
    {
        text(c, t, CX, 80, FONT_SMALL(), col);
    }

    /* Big 7-segment readout in the centre. Drawn solid: the old cyan/magenta
     * chromatic shadow looked the part but blurred the digits. */
    inline void bigTime(LGFX_Sprite* c, const char* t, uint32_t col)
    {
        text(c, t, CX, 118, &fonts::Font7, col);
    }

    /* Secondary readout under the big one (e.g. centiseconds). */
    inline void smallReadout(LGFX_Sprite* c, const char* t, uint32_t col, int y = 162)
    {
        text(c, t, CX, y, FONT_VALUE(), col);
    }

    /* Bottom hint, inside the ring. A hint may pack two actions separated by
     * a run of spaces ("Press to pause   Hold to exit"); they are stacked on
     * two lines so each stays inside the round edge. */
    inline void hint(LGFX_Sprite* c, const char* t, uint32_t col = SLATE)
    {
        const char* sep = strstr(t, "  ");   // run of 2+ spaces = action divider
        if (sep == nullptr)
        {
            text(c, t, CX, 186, FONT_SMALL(), col);
            return;
        }

        char a[48];
        size_t na = (size_t)(sep - t);
        if (na >= sizeof(a)) na = sizeof(a) - 1;
        memcpy(a, t, na);
        a[na] = '\0';

        const char* p = sep;
        while (*p == ' ') p++;

        text(c, a, CX, 180, FONT_SMALL(), col);
        text(c, p, CX, 197, FONT_SMALL(), blend(col, BG, 0.7f));
    }

    /* ---------------- redesigned HUD language (shared by the time apps) ------ */

    /* Boot-in timing: one-shot "decrypt" that plays for BOOT_MS on app open. */
    constexpr uint32_t BOOT_MS = 780;
    inline float bootProgress(uint32_t bootMs) { return bootMs >= BOOT_MS ? 1.0f : (float)bootMs / BOOT_MS; }
    inline bool  booting(uint32_t bootMs)      { return bootMs < BOOT_MS; }

    /* Chamfered (cut-corner) rectangle outline — the CP-style tab edge. */
    inline void chamferOutline(LGFX_Sprite* c, int x, int y, int w, int h, int cut, uint32_t col)
    {
        c->drawLine(x + cut, y,       x + w - cut, y,       col);   // top
        c->drawLine(x + w - cut, y,   x + w, y + cut,       col);   // TR diag
        c->drawLine(x + w, y + cut,   x + w, y + h - cut,   col);   // right
        c->drawLine(x + w, y + h - cut, x + w - cut, y + h, col);   // BR diag
        c->drawLine(x + w - cut, y + h, x + cut, y + h,     col);   // bottom
        c->drawLine(x + cut, y + h,   x, y + h - cut,       col);   // BL diag
        c->drawLine(x, y + h - cut,   x, y + cut,           col);   // left
        c->drawLine(x, y + cut,       x + cut, y,           col);   // TL diag
    }

    /* Screen name, centred near the top, with a yellow underline that grows
     * with `frac` — the boot-in wipes it in. */
    inline void header(LGFX_Sprite* c, const char* name, uint32_t accent, float frac = 1.0f)
    {
        text(c, name, CX, 50, FONT_LABEL(), blend(accent, WHITE, 0.75f));
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        int w = c->textWidth(name);
        c->fillRect(CX - w / 2, 62, (int)(w * frac), 2, AMBER);
    }

    /* State line under the header: a dot and a word ("Running", "Paused").
     * The dot pulses while `live` so a running clock reads as running. */
    inline void chip(LGFX_Sprite* c, const char* state, uint32_t accent, bool live = false)
    {
        c->setFont(FONT_SMALL());
        int w = c->textWidth(state) + 12;
        int x = CX - w / 2;
        bool on = !live || ((millis() / 500) % 2) == 0;
        c->fillSmoothCircle(x + 3, 77, 3, on ? accent : blend(accent, BG, 0.35f));
        text(c, state, x + 12, 77, FONT_SMALL(), accent, textdatum_t::middle_left);
    }

    /* One-shot scanline sweep overlay for the boot-in (frac 0..1 -> top..bottom). */
    inline void scanlineSweep(LGFX_Sprite* c, uint32_t accent, float frac)
    {
        int yy = (int)(frac * 240.0f);
        for (int k = 0; k < 14; k++)
            c->drawFastHLine(0, yy - k, 240, blend(accent, BG, 0.30f * (1.0f - k / 14.0f)));
        c->drawFastHLine(0, yy, 240, blend(WHITE, BG, 0.5f));
    }

    /* Copy `real` into `dst`, replacing not-yet-locked digits with scrambling
     * glyphs during the boot-in (locks left-to-right). dst must fit real+1. */
    inline void scrambleTime(char* dst, const char* real, uint32_t bootMs)
    {
        int i = 0;
        for (; real[i] != '\0'; i++)
        {
            char ch = real[i];
            uint32_t lockAt = 120u + (uint32_t)i * 110u;
            if (ch >= '0' && ch <= '9' && bootMs < lockAt)
                dst[i] = (char)('0' + (int)(((millis() / 40) + i * 7) % 10));
            else
                dst[i] = ch;
        }
        dst[i] = '\0';
    }

    /* Full HUD chrome for a time app: dark ground + header + state line,
     * plus the scanline sweep while booting. The caller then draws the ring
     * (scaled by bootProgress) and readout. */
    inline void hudChrome(LGFX_Sprite* c, const char* name, const char* state,
                          uint32_t accent, uint32_t bootMs, bool live = false)
    {
        background(c, accent);
        header(c, name, accent, bootProgress(bootMs));
        chip(c, state, accent, live);
        if (booting(bootMs)) scanlineSweep(c, accent, bootProgress(bootMs));
    }

}
