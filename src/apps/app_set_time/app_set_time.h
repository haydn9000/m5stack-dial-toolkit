/**
 * @file app_set_time.h
 * @brief Set the PCF8563 RTC on-device, cyberpunk-styled to match the watch face.
 *
 *  Controls
 *   - Turn dial  : adjust the highlighted field
 *   - Short press: move to the next field (year -> month -> day -> hour -> minute)
 *   - Long press : save to the RTC and exit
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"


namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace SET_TIME
        {
            enum Field_t { F_YEAR = 0, F_MON, F_DAY, F_HOUR, F_MIN, F_COUNT };

            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                int field = F_YEAR;
                int year  = 2026;
                int mon   = 1;    // 1-12
                int day   = 1;    // 1-31
                int hour  = 0;    // 0-23
                int min   = 0;    // 0-59

                int64_t  last_raw    = 0;
                uint32_t last_render = 0;
            };
        }

        class SetTime : public APP_BASE
        {
            private:
                const char* _tag = "SetTime";

                void _adjust(int delta);
                void _save();
                void _render();

            public:
                SET_TIME::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
