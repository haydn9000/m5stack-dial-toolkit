/**
 * @file app_pc_stats.h
 * @brief PC Stats — a triangular cluster of three independent ring gauges
 *        (CPU / RAM / GPU), fed by PC_LINK over BLE/USB serial. Ported
 *        from the wiodeck project's sysStats.cpp.
 *
 *  Controls
 *   - Long press : exit to menu
 */
#pragma once
#include "../app.h"
#include "../../hal/hal.h"

namespace MOONCAKE
{
    namespace USER_APP
    {
        namespace PC_STATS
        {
            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                uint16_t drawn_version = 0;
                bool     drawn_valid   = false;
                uint32_t last_data_ms  = 0;
            };
        }

        class PcStats : public APP_BASE
        {
            private:
                const char* _tag = "PcStats";

                void _render();

            public:
                PC_STATS::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
