/**
 * @file app_time_sync.h
 * @brief Internet time — shows the WiFi/SNTP clock sync status, how far the
 *        RTC had drifted, and runs the phone-based WiFi setup (TIME_SYNC
 *        captive portal, joined by scanning an on-screen QR code).
 *
 *  Controls
 *   - Turn       : choose an action (Sync now / Set up Wi-Fi)
 *   - Short press: run it
 *   - Long press : exit (or cancel setup)
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"

namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace TIME_SYNC_APP
        {
            enum Action_t { A_SYNC = 0, A_SETUP, A_COUNT };

            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                int      action      = A_SYNC;
                bool     portal      = false;
                uint32_t saved_at    = 0;      // when the phone saved settings
                int64_t  last_raw    = 0;
                uint32_t last_render = 0;
                uint32_t boot_start  = 0;
            };
        }

        class TimeSyncApp : public APP_BASE
        {
            private:
                const char* _tag = "TimeSync";

                void _render_main();
                void _render_portal();

            public:
                TIME_SYNC_APP::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
