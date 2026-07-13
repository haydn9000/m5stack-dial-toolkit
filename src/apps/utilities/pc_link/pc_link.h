/**
 * @file pc_link.h
 * @brief Shared BLE + USB-serial data channel for the Claude Usage and PC
 *        Stats apps. Ported from the wiodeck project's claudeUsage.cpp /
 *        sysStats.cpp / bluetooth.cpp — same JSON wire schema and parsing
 *        approach, retargeted onto this project's NimBLE-Arduino stack.
 */
#pragma once
#include <stdint.h>

namespace PC_LINK
{
    struct ClaudeUsageData
    {
        float session_pct        = 0.0f;
        int   session_reset_mins = 0;
        float weekly_pct         = 0.0f;
        int   weekly_reset_mins  = 0;
        char  status[16]         = "unknown";
        char  session_reset_str[56] = "";
        char  weekly_reset_str[56]  = "";
        bool  valid = false;
    };

    struct PcStatsData
    {
        int  cpu_pct     = 0;
        int  cpu_temp    = -1;
        int  ram_pct     = 0;
        char ram_str[20] = "";
        int  gpu_pct     = -1;
        int  gpu_temp    = -1;
        char gpu_name[32] = "";
        bool valid = false;
    };

    extern ClaudeUsageData claudeUsage;
    extern uint16_t        claudeUsageVersion;
    extern PcStatsData     pcStats;
    extern uint16_t        pcStatsVersion;

    /* Call once, from main.cpp's setup() after hal.init(): starts Serial
     * and prepares (but does not advertise) the BLE GATT service. */
    void init();

    /* Start/stop BLE advertising. Call setActive(true) from an app's
     * onCreate() and setActive(false) from its onDestroy(). */
    void setActive(bool active);

    /* Call every onRunning(): drains pending serial/BLE data and updates
     * claudeUsage/pcStats + their version counters. */
    void poll();

    /* BLE MAC address string, for the no-data screen's "ADDR:" line. */
    const char* bleAddress();
}
