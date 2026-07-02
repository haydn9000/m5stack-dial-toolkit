/**
 * @file app_timer.h
 * @brief Cyberpunk-themed countdown timer app for the M5Dial.
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"


namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace TIMER_APP
        {
            enum State_t { SETTING = 0, RUNNING, PAUSED, FINISHED };

            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                State_t  state       = SETTING;
                int      set_seconds = 300;   // selected duration (default 5:00)
                uint32_t duration_ms = 0;     // duration of the active countdown
                uint32_t end_ts      = 0;     // millis() when it reaches zero
                uint32_t remaining   = 0;     // banked remaining ms while paused
                int64_t  last_raw    = 0;
                uint32_t last_render = 0;
                uint32_t alarm_ts    = 0;     // next alarm beep time
                uint32_t boot_start  = 0;     // millis() at open, for the boot-in
            };
        }

        class Timer : public APP_BASE
        {
            private:
                const char* _tag = "Timer";

                uint32_t _remaining_ms();
                void     _render();

            public:
                TIMER_APP::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
