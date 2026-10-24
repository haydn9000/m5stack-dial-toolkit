/**
 * @file wifi_common_test.cpp
 * @author Forairaaaaa
 * @brief WiFi scan helpers for the WiFi Scan app.
 * @version 0.2
 * @date 2023-08-04
 *
 * @copyright Copyright (c) 2023
 *
 * Reimplemented on the Arduino WiFi library. The original drove esp_wifi
 * directly — creating and deleting the default event loop and calling
 * nvs_flash_deinit() — which aborts once Arduino WiFi (used by TIME_SYNC)
 * owns the event loop, and pulls NVS out from under Preferences. The radio
 * is shared with TIME_SYNC through its radio lock.
 */
#include "wifi_common_test.h"
#include "../time_sync/time_sync.h"
#include <WiFi.h>


namespace WIFI_COMMON_TEST
{
    void nvs_init()
    {
        /* NVS is initialised by the Arduino core (Preferences / WiFi). */
        is_nvs_inited = true;
    }

    namespace SCAN
    {
        static int _scan_raw()
        {
            TIME_SYNC::RadioLock lock;
            WiFi.mode(WIFI_STA);
            int n = WiFi.scanNetworks();
            return n < 0 ? 0 : n;
        }

        static void _scan_done()
        {
            WiFi.scanDelete();
            TIME_SYNC::radioIdle();
        }

        uint8_t scan(std::string& result)
        {
            int n = _scan_raw();
            result = "";
            for (int i = 0; i < n && i < 20; i++)
            {
                result += std::to_string(i);
                result += ". (";
                result += std::to_string(WiFi.RSSI(i));
                result += ") ";
                result += WiFi.SSID(i).c_str();
                result += "\n";
            }
            _scan_done();
            return (uint8_t)n;
        }

        void scan(std::vector<ScanResult_t>& scanResult)
        {
            int n = _scan_raw();
            std::vector<ScanResult_t>().swap(scanResult);
            for (int i = 0; i < n && i < 20; i++)
            {
                ScanResult_t r;
                r.ssid = WiFi.SSID(i).c_str();
                r.rssi = (int8_t)WiFi.RSSI(i);
                scanResult.push_back(r);
            }
            _scan_done();
        }
    }
}
