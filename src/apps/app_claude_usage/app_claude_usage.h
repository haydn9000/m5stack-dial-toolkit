/**
 * @file app_claude_usage.h
 * @brief Claude Usage — session (5h) utilization as the big number + outer
 *        pip ring, weekly (7d) as a labelled bar underneath, each with a
 *        "resets in …" countdown. Fed by PC_LINK over BLE/USB serial.
 *        Ported from the wiodeck project's claudeUsage.cpp.
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
        namespace CLAUDE_USAGE
        {
            struct Data_t
            {
                HAL::HAL* hal = nullptr;

                uint16_t drawn_version = 0;
                bool     drawn_valid   = false;
                uint32_t last_data_ms  = 0;
            };
        }

        class ClaudeUsage : public APP_BASE
        {
            private:
                const char* _tag = "ClaudeUsage";

                void _render();

            public:
                CLAUDE_USAGE::Data_t _data;

                void onSetup();
                void onCreate();
                void onRunning();
                void onDestroy();
        };
    }
}
