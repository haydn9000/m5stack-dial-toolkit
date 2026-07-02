/**
 * @file app_stopwatch.h
 * @brief Cyberpunk-themed stopwatch app for the M5Dial.
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"


namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace STOPWATCH
        {
            enum State_t { STOPPED = 0, RUNNING, PAUSED };

            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                State_t  state        = STOPPED;
                uint32_t accumulated  = 0;   // ms banked while paused
                uint32_t start_ts     = 0;   // millis() when last (re)started
                int64_t  last_raw     = 0;   // encoder baseline
                uint32_t last_render  = 0;
                uint32_t boot_start   = 0;   // millis() at open, for the boot-in
            };
        }

        class Stopwatch : public APP_BASE
        {
            private:
                const char* _tag = "Stopwatch";

                uint32_t _elapsed();
                void     _render();

            public:
                STOPWATCH::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
