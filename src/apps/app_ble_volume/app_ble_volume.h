/**
 * @file app_ble_volume.h
 * @brief BLE HID volume controller app.
 *        Pairs with a computer/phone as a BLE keyboard and sends
 *        media volume-up/down/mute keys via the rotary encoder.
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"
#include "gui/gui_ble_volume.h"


namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace BLE_VOLUME
        {
            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                /* Local volume tracker (0-100). Starts at 50 since we can't
                   read the actual OS volume — it just reflects cumulative presses. */
                int  volume   = 50;
                bool muted    = false;

                /* Scroll speed acceleration (mirrors brightness app pattern) */
                uint32_t scroll_speed_time_count = 0;
                uint32_t delta_time              = 0;
                int      volume_increment        = 1;

                /* UI refresh throttle */
                uint32_t page_update_time_count  = 0;
                uint32_t page_update_interval    = 80;  /* ms */
            };
        }

        class BLE_Volume : public APP_BASE
        {
            private:
                const char* _tag = "BLE_Volume";
                void _ble_init();

                /* Saved global encoder beep callback, suppressed while this app
                   is open so the knob only beeps at the extremes (0/100). */
                void (*_saved_move_cb)(ESP32Encoder*, void*) = nullptr;
                void* _saved_move_userdata = nullptr;

                /* Last consumed raw quadrature count. We read the raw encoder
                   count directly (not readCount()/2) so half-step contact bounce
                   at the extremes can't register a spurious reverse tick. */
                int64_t _last_raw = 0;

            public:
                BLE_VOLUME::Data_t _data;
                GUI_BLE_Volume _gui;

                GUI_Base* getGui() override { return &_gui; }

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
