/**
 * @file app_pomodoro.h
 * @brief Cyberpunk-themed Pomodoro focus timer for the M5Dial.
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"


namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace POMODORO
        {
            enum Phase_t { WORK = 0, SHORT_BREAK, LONG_BREAK };
            enum Run_t   { IDLE = 0, RUNNING, PAUSED };

            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                Phase_t  phase     = WORK;
                Run_t    run       = IDLE;
                int      work_done = 0;       // completed focus blocks in this cycle (0..4)

                uint32_t duration_ms = 0;
                uint32_t end_ts      = 0;
                uint32_t remaining   = 0;
                int64_t  last_raw    = 0;
                uint32_t last_render = 0;
                uint32_t boot_start  = 0;     // millis() at open, for the boot-in
            };
        }

        class Pomodoro : public APP_BASE
        {
            private:
                const char* _tag = "Pomodoro";

                uint32_t _phase_duration(POMODORO::Phase_t p);
                uint32_t _remaining_ms();
                void     _advance();          // auto-transition on completion
                void     _render();

            public:
                POMODORO::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
