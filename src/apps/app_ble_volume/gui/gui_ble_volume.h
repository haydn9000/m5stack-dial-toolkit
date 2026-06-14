/**
 * @file gui_ble_volume.h
 */
#pragma once
#include "../../utilities/gui_base/gui_base.h"


class GUI_BLE_Volume : public GUI_Base
{
    public:
        void init() override;
        void renderPage(bool connected, int volume, bool muted);

    private:
        void _draw_background();
        void _draw_scanlines();
        void _draw_hud_frame(bool muted);
        void _draw_gauge(int volume, bool muted);
        void _draw_status(bool connected);
        void _draw_center_number(int volume, bool muted);
};
