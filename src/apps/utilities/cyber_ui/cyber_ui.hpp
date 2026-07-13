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
    /* --- Neon cyberpunk palette: electric yellow + neon cyan + hot red on
     *     near-black. The AMBER slot carries the signature electric yellow. --- */
    constexpr uint32_t BG       = 0x070B12;  // near-black, cool tint
    constexpr uint32_t SCANLINE = 0x0C1420;  // faint CRT scanline
    constexpr uint32_t TRACK    = 0x13293A;  // dim ring track
    constexpr uint32_t BORDER   = 0x1F4A5E;  // ring edge line
    constexpr uint32_t CYAN     = 0x00F0FF;  // electric cyan
    constexpr uint32_t MAGENTA  = 0xFF2A6D;  // hot pink-magenta
    constexpr uint32_t AMBER    = 0xFCEE0A;  // signature electric yellow
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

    /* Asymmetric HUD frame: a bold double bracket upper-left, a thin bracket
     * lower-right and a short telemetry tick rail on the left, plus the neon
     * outer rim. Everything sits inside the ring (r < 100) so it never collides
     * with the gauge — a deliberately off-balance "terminal" frame. */
    inline void hudFrame(LGFX_Sprite* c, uint32_t accent)
    {
        const uint32_t bold  = blend(accent, BG, 0.85f);
        const uint32_t mid   = blend(accent, BG, 0.5f);
        const uint32_t faint = blend(accent, BG, 0.35f);
        const uint32_t rail  = blend(DIMTEXT, BG, 0.9f);

        /* Bold double bracket, upper-left (2px) */
        c->drawLine(40, 74, 40, 58, bold);  c->drawLine(41, 74, 41, 58, bold);
        c->drawLine(40, 58, 66, 58, bold);  c->drawLine(40, 59, 66, 59, bold);
        c->drawLine(44, 78, 44, 62, faint); c->drawLine(44, 62, 70, 62, faint);  // inner thin

        /* Thin single bracket, lower-right */
        c->drawLine(200, 166, 200, 182, mid);
        c->drawLine(200, 182, 174, 182, mid);

        /* Left telemetry tick rail + label */
        for (int i = 0; i < 6; i++)
        {
            int yy = 100 + i * 8;
            c->drawFastHLine(30, yy, (i % 3 == 0) ? 9 : 5, rail);
        }
        c->setFont(&fonts::Font0);
        c->setTextSize(1);
        c->setTextColor(rail);
        c->setTextDatum(textdatum_t::middle_left);
        c->drawString("SIG", 30, 90);
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

        /* No solid ring track — the gauge pips float on black (matches the
         * design prototype). The rim line is drawn by progressRing(). */
        hudFrame(c, accent);
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

    /* Big 7-segment time readout in the centre with a glitchy cyan/magenta
     * chromatic-aberration shadow: a steady ±3 split that periodically tears
     * wider and jitters vertically for a digital-glitch flicker. */
    inline void bigTime(LGFX_Sprite* c, const char* t, uint32_t col, float glitch = 1.0f)
    {
        c->setFont(&fonts::Font7);
        c->setTextSize(1);
        c->setTextDatum(textdatum_t::middle_center);

        /* Steady chromatic shadow, with an occasional brief horizontal shimmer.
         * `glitch` scales it: 0 = steady only, 1 = default, >1 = livelier. */
        uint32_t ph = millis();
        int gx = 3;
        if (glitch > 0.01f)
        {
            uint32_t window = (uint32_t)(70.0f * glitch);   // ms of shimmer per 3s
            if ((ph % 3000u) < window)
                gx = 4 + (int)((ph / 40u) % 2u);            // 4..5, momentary
        }

        c->setTextColor(blend(MAGENTA, BG, 0.62f));
        c->drawString(t, CX + gx, 118);
        c->setTextColor(blend(CYAN, BG, 0.62f));
        c->drawString(t, CX - gx, 118);
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

    /* Chamfered header tab, centred, with the screen name and a yellow highlight
     * underline that grows with `frac` — the boot-in wipes it in. Matches the
     * design/timeapp-hud.html header (x62,w116). */
    inline void header(LGFX_Sprite* c, const char* name, uint32_t accent, float frac = 1.0f)
    {
        const int x = 62, y = 44, w = 116, h = 17, cut = 5;
        c->fillRect(x + 1, y + 1, w - 2, h - 2, blend(accent, BG, 0.10f));
        chamferOutline(c, x, y, w, h, cut, blend(accent, BG, 0.55f));
        c->setFont(&fonts::Font0);
        c->setTextSize(1);
        c->setTextColor(blend(accent, BG, 0.95f));
        c->setTextDatum(textdatum_t::middle_center);
        c->drawString(name, CX, y + h / 2);
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        c->fillRect(x + 4, y + h - 1, (int)((w - 8) * frac), 2, AMBER);   // yellow highlight
    }

    /* Status chip (upper-right, tabbed onto the header) with a blinking dot.
     * `code` is a short status like "RUN" / "PSE" / "END". */
    inline void chip(LGFX_Sprite* c, const char* code, uint32_t accent)
    {
        const int x = 148, y = 58, w = 44, h = 16, cut = 4;
        c->fillRect(x + 1, y + 1, w - 2, h - 2, blend(accent, BG, 0.12f));
        chamferOutline(c, x, y, w, h, cut, blend(accent, BG, 0.6f));
        bool on = ((millis() / 400) % 2) == 0;
        c->fillCircle(x + 9, y + h / 2, 2, on ? accent : blend(accent, BG, 0.35f));
        c->setFont(&fonts::Font0);
        c->setTextSize(1);
        c->setTextColor(blend(accent, BG, 0.9f));
        c->setTextDatum(textdatum_t::middle_left);
        c->drawString(code, x + 16, y + h / 2);
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

    /* Full HUD chrome for a time app: dark ground + asym frame + header tab +
     * status chip, plus the scanline sweep while booting. The caller then draws
     * the ring (scaled by bootProgress) and readout. */
    inline void hudChrome(LGFX_Sprite* c, const char* name, const char* code,
                          uint32_t accent, uint32_t bootMs)
    {
        background(c, accent);
        header(c, name, accent, bootProgress(bootMs));
        chip(c, code, accent);
        if (booting(bootMs)) scanlineSweep(c, accent, bootProgress(bootMs));
    }

}
