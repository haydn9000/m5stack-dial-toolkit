/**
 * @file launcher_render_callback.hpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-07-25
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include "launcher_icons/launcher_icons.h"
#include "launcher_icons/launcher_glyphs.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include <iostream>
#include <array>


/* Render setting */
#define THEME_COLOR_FG              (uint32_t)0xFCEE0A
// #define THEME_COLOR_BG              (uint32_t)0x424242
#define THEME_COLOR_BG              TFT_BLACK
#define SELECTOR_COLOR              (uint32_t)0xEAF6FF
#define SELECTOR_RADIUS             5
#define ICON_RADIUS                 22
#define ICON_SELECTED_R_OFFSET      3
#define ICON_TAG_COLOR              (uint32_t)0xFCEE0A
#define ICON_TAG_UP_OFFSET          -24
#define ICON_TAG_DOWN_OFFSET        0

#define ICON_NUM                    9


/* Strucut to hold icon value */
struct Icon_t
{
    std::string tag_up;
    std::string tag_down;
    void* icon_pic;
    uint32_t color;
    int x;
    int y;
};

/* Icon list */
static std::array<Icon_t, ICON_NUM> icon_list;
/* Icon color */
/* Neon cyberpunk ring: electric cyan / yellow / hot red spread across icons */
static std::array<uint32_t, ICON_NUM> icon_color_list = {
    0x00F0FF,  // Watch Face   - electric cyan (home / default landing app)
    0xFF2A6D,  // BLE Volume   - hot pink-magenta
    0xFCEE0A,  // Timer        - signature yellow
    0x00FF9F,  // Stopwatch    - neon mint
    0xFF003C,  // Pomodoro     - alert red
    0xFF9E00,  // Brightness   - warm amber
    0xD97757,  // Claude Usage - coral
    0xA98BFF,  // PC Stats     - lavender (cyan is the watch's)
    0x8FB0C2   // More         - slate
};
/* Icon tag: app name, then a few words on what it does */
static std::array<std::string, ICON_NUM * 2> icon_tag_list = {
    "Watch",      "Time and date",
    "Volume",     "Media volume",
    "Timer",      "Count down",
    "Stopwatch",  "Count up",
    "Pomodoro",   "Focus sessions",
    "Brightness", "Screen light",
    "Claude",     "Usage limits",
    "PC stats",   "CPU, RAM, GPU",
    "More",       "Tools and settings"
};
/* Line-icon masks (Tabler Icons, tools/gen_tabler_icons.py) */
static std::array<const uint8_t*, ICON_NUM> icon_glyph_list = {
    launcher_glyph_watch,
    launcher_glyph_volume,
    launcher_glyph_timer,
    launcher_glyph_stopwatch,
    launcher_glyph_pomodoro,
    launcher_glyph_brightness,
    launcher_glyph_claude,
    launcher_glyph_pcstats,
    launcher_glyph_more
};
/* Icons are drawn straight from their masks every frame rather than cached
 * in sprites: this board has no PSRAM, and 18 cached 42x42 sprites (~63KB)
 * left too little internal RAM for WiFi to start (TIME_SYNC). */

/* Dark disc behind an idle icon */
inline uint32_t icon_idle_disc(uint32_t color) { return CYBER::blend(color, CYBER::BG, 0.18f); }

/* Draw a glyph mask centred on (cx, cy): ink blended over the disc it sits
 * on, so the antialiased edge matches; pixels outside the glyph untouched. */
inline void icon_draw(LovyanGFX* dst, const uint8_t* mask, int cx, int cy, uint32_t ink, uint32_t disc)
{
    int x0 = cx - 21, y0 = cy - 21;
    for (int i = 0; i < 42 * 42; i++)
    {
        if (mask[i] < 24) continue;
        dst->drawPixel(x0 + i % 42, y0 + i / 42, CYBER::blend(ink, disc, mask[i] / 255.0f));
    }
}

/* Tint a glyph mask into a 42x42 sprite with TFT_BLACK as the clear colour —
 * the icon handed to apps that take one (GUI_Base). */
inline void icon_tint(LGFX_Sprite& s, const uint8_t* mask, uint32_t ink, uint32_t disc)
{
    s.createSprite(42, 42);
    s.fillSprite(TFT_BLACK);
    for (int i = 0; i < 42 * 42; i++)
    {
        if (mask[i] < 24) continue;
        uint32_t c = CYBER::blend(ink, disc, mask[i] / 255.0f);
        if (lgfx::color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF) == 0)
            c = 0x080808;   // keep near-black ink opaque, not keyed out
        s.drawPixel(i % 42, i / 42, c);
    }
}


struct LauncherRender_CB_t : public SMOOTH_MENU::SimpleMenuCallback_t
{
    private:
        LGFX_Sprite* _canvas;

    public:
        // LauncherRender_CB_t() = default;
        // ~LauncherRender_CB_t() = default;

        inline void setCanvas(LGFX_Sprite* canvasPtr) { _canvas = canvasPtr; }

        /* Override render callback */
        void renderCallback(
            const std::vector<SMOOTH_MENU::Item_t*>& menuItemList,
            const SMOOTH_MENU::RenderAttribute_t& selector,
            const SMOOTH_MENU::RenderAttribute_t& camera
        ) override
        {
            /* Clear screen */
            _canvas->fillScreen(THEME_COLOR_BG);

            /* No selector dot: the enlarged, filled icon marks the selection
             * and the dot only collided with the centre label. */
            // printf("%d %d\n", selector.x, selector.y);
            // printf("%d %d\n", menuItemList[selector.targetItem]->x, menuItemList[selector.targetItem]->y);


            /* Draw icon that moves with menu item */
            int icon_r = 190;
            int x;
            int y;
            for (int i = 0; i < icon_list.size(); i++)
            {
                /* Calculate position */
                x = (menuItemList[i]->x - 120) * icon_r / 120 + 120;
                y = (menuItemList[i]->y - 120) * icon_r / 120 + 120;

                uint32_t col = icon_list[i].color;
                if (i == selector.targetItem)
                {
                    _canvas->fillSmoothCircle(x, y, selector.width + ICON_SELECTED_R_OFFSET, col);
                    icon_draw(_canvas, icon_glyph_list[i], x, y, CYBER::BG, col);
                }
                else
                {
                    uint32_t disc = icon_idle_disc(col);
                    _canvas->fillSmoothCircle(x, y, ICON_RADIUS, disc);
                    _canvas->drawCircle(x, y, ICON_RADIUS, CYBER::blend(col, CYBER::BG, 0.45f));
                    icon_draw(_canvas, icon_glyph_list[i], x, y, col, disc);
                }
            }


            /* Draw Icon tag */
            /* Get Icon offset position */
            int new_r = 20;
            int icon_tag_x = (selector.x - 120) * new_r / 120 + 120;
            int icon_tag_y = (selector.y - 120) * new_r / 120 + 120;

            /* Always fix the text size — an app just closed may have left the
             * canvas at a larger size, which would blow up the tag text. */
            _canvas->setTextSize(1);

            /* If pointing a icon: its name, tinted with its colour, and what
             * it does underneath */
            if (selector.targetItem < icon_list.size())
            {
                const Icon_t& icon = icon_list[selector.targetItem];
                CYBER::text(_canvas, icon.tag_up.c_str(), 120, 110,
                            CYBER::FONT_VALUE(), CYBER::blend(icon.color, CYBER::WHITE, 0.55f));
                CYBER::text(_canvas, icon.tag_down.c_str(), 120, 136,
                            CYBER::FONT_SMALL(), CYBER::SLATE);
            }
            else 
            {
                _canvas->drawCenterString(
                    "-. -",
                    icon_tag_x,
                    icon_tag_y + ICON_TAG_UP_OFFSET
                );
            }


            // _canvas->fillScreen(TFT_BLACK);

            // _canvas->pushSprite(0, 0);
        }
};



