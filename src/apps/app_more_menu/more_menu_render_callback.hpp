/**
 * @file more_menu_render_callback.hpp
 * @author Forairaaaaa
 * @brief 
 * @version 0.1
 * @date 2023-08-07
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include "../utilities/cyber_ui/cyber_ui.hpp"


/* Render setting */
#define MORE_MENU_COLOR_SELECTOR           (uint32_t)0xfa7000
#define MORE_MENU_COLOR_BG                  TFT_BLACK


struct MoreMenuRender_CB_t : public SMOOTH_MENU::SimpleMenuCallback_t
{
    private:
        LGFX_Sprite* _canvas;
        LGFX_Sprite* _icon_canvas = nullptr;
        std::vector<const uint16_t*> _icons;

    public:
        inline void setCanvas(LGFX_Sprite* canvasPtr) { _canvas = canvasPtr; }
        inline void setIconCanvas(LGFX_Sprite* iconCanvas) { _icon_canvas = iconCanvas; }
        inline void setIcons(const std::vector<const uint16_t*>& icons) { _icons = icons; }

        /* Override render callback */
        void renderCallback(
            const std::vector<SMOOTH_MENU::Item_t*>& menuItemList,
            const SMOOTH_MENU::RenderAttribute_t& selector,
            const SMOOTH_MENU::RenderAttribute_t& camera
        ) override
        {
            _canvas->fillScreen(CYBER::BG);

            /* Centred list on the round panel: the selected row sits on the
             * horizontal midline in a highlight pill; the others fade with
             * distance and are dropped before they reach the narrow top and
             * bottom of the circle, so no label ever runs off the edge. */
            const int   mid_y   = 128;
            const float spacing = 1.25f;   // menu rows are 26px; spread to ~32px

            CYBER::text(_canvas, "More", 120, 36, CYBER::FONT_LABEL(),
                        CYBER::blend(MORE_MENU_COLOR_SELECTOR, CYBER::WHITE, 0.8f));

            /* Pill behind the selected row (fixed — the list scrolls under it) */
            _canvas->fillSmoothRoundRect(30, mid_y - 17, 180, 34, 17,
                                         CYBER::blend(MORE_MENU_COLOR_SELECTOR, CYBER::BG, 0.16f));
            _canvas->drawRoundRect(30, mid_y - 17, 180, 34, 17,
                                   CYBER::blend(MORE_MENU_COLOR_SELECTOR, CYBER::BG, 0.6f));

            _canvas->setTextSize(1);
            for (int i = 0; i < (int)menuItemList.size(); i++)
            {
                float dy = (float)(menuItemList[i]->y - selector.y);
                int   y  = mid_y + (int)(dy * spacing);
                if (y < 66 || y > 206)
                    continue;

                float dist = fabsf(dy) / 26.0f;           // rows from the selection
                bool  sel  = dist < 0.5f;
                uint32_t col = sel ? CYBER::WHITE
                                   : CYBER::blend(CYBER::SLATE, CYBER::BG, 1.0f - 0.28f * dist);

                const char* tag = menuItemList[i]->tag.c_str();
                _canvas->setFont(CYBER::FONT_LABEL());
                int tw = _canvas->textWidth(tag);
                bool has_icon = _icon_canvas != nullptr && i < (int)_icons.size() && _icons[i] != nullptr;
                int x0 = 120 - (tw + (has_icon ? 28 : 0)) / 2;

                if (has_icon)
                {
                    _icon_canvas->pushImage(0, 0, 42, 42, _icons[i]);
                    _icon_canvas->pushRotateZoom(_canvas, x0 + 11, y, 0, 0.5, 0.5, TFT_BLACK);
                    x0 += 28;
                }
                CYBER::text(_canvas, tag, x0, y, CYBER::FONT_LABEL(), col, textdatum_t::middle_left);
            }

            _canvas->pushSprite(0, 0);
        }
};