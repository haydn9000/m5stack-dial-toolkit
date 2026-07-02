/**
 * @file app_watchface.h
 * @brief SCOPE — a radar-scope watch face for the M5Dial.
 *
 *  The round display is a radar scope: a sweep line rotates the rim once per
 *  minute (its angle = seconds), dragging a decaying cyan phosphor trail and
 *  igniting the rim tick it passes. HH:MM sits in the centre (Font7, with the
 *  cyber_ui chromatic-aberration ghosts).
 *
 *  Controls
 *   - Turn dial  : reveal the DATE layer (weekday / date / month progress),
 *                  which auto-reverts to the time after a few seconds
 *   - Long press : exit to menu
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"
#include <ctime>


namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace WATCHFACE
        {
            enum View_t { TIME = 0, DATE };

            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                View_t   view        = TIME;
                uint32_t date_until  = 0;    // millis() deadline to auto-revert to TIME
                uint32_t turn_lock   = 0;    // millis() until which turns are ignored (one twist = one switch)

                int64_t  last_raw    = 0;    // encoder baseline (half-quad raw counts)
                int      last_sec    = -1;   // last observed RTC second
                uint32_t sec_epoch   = 0;    // millis() when last_sec began (for smooth sweep)
                uint32_t last_render = 0;

                tm       now = {};           // most recent RTC read (zero-init: safe before first getTime)
            };
        }

        class Watchface : public APP_BASE
        {
            private:
                const char* _tag = "Watchface";

                void _render_time(float sec_float);
                void _render_date();

            public:
                WATCHFACE::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
