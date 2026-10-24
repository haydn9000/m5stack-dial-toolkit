/**
 * @file time_sync.h
 * @brief Keeps the PCF8563 RTC on internet time over WiFi (SNTP).
 *
 * A background FreeRTOS task joins the saved network, waits for an SNTP
 * fix, writes local time into the RTC on a whole-second boundary and turns
 * the radio off again. It runs at boot, every SYNC_INTERVAL_H hours after a
 * success, sooner (RETRY_MIN) after a failure, and on requestSync().
 *
 * Each sync also records how far the RTC had drifted (RTC minus internet
 * time), persisted in NVS, so a slow clock shows up as a number instead of
 * a mystery.
 *
 * Credentials and timezone are entered from a phone: startPortal() turns
 * the dial into an open access point ("M5Dial-Setup") with a captive setup
 * page; saving there stores them in NVS (namespace "settings", keys
 * "wifi_ssid" / "wifi_pass" / "tz") and starts a sync.
 *
 * The RTC stores local time (the build-time seed and Set Time both do), so
 * the timezone is a POSIX TZ string, e.g. "EST5EDT,M3.2.0,M11.1.0".
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace HAL { class HAL; }

namespace TIME_SYNC
{
    constexpr int SYNC_INTERVAL_H = 6;
    constexpr int RETRY_MIN       = 15;
    constexpr const char* AP_SSID = "M5Dial-Setup";

    enum State : uint8_t
    {
        NOT_SET_UP,   // no saved network
        IDLE,         // waiting for the next scheduled sync
        CONNECTING,
        SYNCING,      // joined, waiting for SNTP
        SYNCED,       // last attempt succeeded
        FAILED,       // last attempt failed — see lastError()
        PORTAL,       // setup access point is up
    };

    void begin(HAL::HAL* hal);

    State       state();
    const char* lastError();
    const char* ssid();
    bool        hasCredentials();

    /* Wake the task for an immediate sync (no-op without credentials). */
    void requestSync();

    /* Seconds since the last successful sync this boot, or -1 if none. */
    int32_t secondsSinceSync();
    /* RTC minus internet time at the last sync (positive = RTC was fast,
     * negative = slow), and whether it has ever been measured. */
    bool lastDrift(int32_t& driftSec);
    /* Average drift rate between the last two syncs, in seconds per day
     * (same sign convention). A steady rate is the crystal; a lone big jump
     * with rtcHadStopped() is a power loss. */
    bool driftRate(float& secPerDay);
    /* The RTC reported (VL flag) that its oscillator had stopped since it was
     * last set — its supply dropped too low, so its time froze for a while. */
    bool rtcHadStopped();
    /* Call when the RTC is set by hand, so the next sync doesn't compute a
     * drift rate across the manual change. */
    void forgetSyncPoint();

    /* One short line for the watch face ("Synced 2h ago", "Wi-Fi not set
     * up", ...). Returns true when the clock synced recently (< 1 day). */
    bool statusLine(char* buf, size_t n);

    /* Setup access point + captive page. portalPoll() must be called often
     * while it's up. Returns true once credentials were saved. */
    void startPortal();
    bool portalPoll();
    void stopPortal();

    void saveCredentials(const char* ssid, const char* pass, const char* tz);

    /* Exclusive use of the WiFi radio (the background sync and the WiFi Scan
     * app both need it). radioIdle() turns the radio off unless a sync or the
     * portal is using it. */
    struct RadioLock
    {
        RadioLock();
        ~RadioLock();
    };
    void radioIdle();
}
