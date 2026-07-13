/**
 * @file pc_link.cpp
 * @brief See pc_link.h.
 */
#include "pc_link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>

namespace PC_LINK
{
    static const char* TAG = "pc_link";

    #define PC_LINK_DEVICE_NAME  "M5DIAL-001"
    #define PC_LINK_SERVICE_UUID "4d354449-414c-0000-0000-000000000001"
    #define PC_LINK_RX_CHAR_UUID "4d354449-414c-0000-0000-000000000002"

    ClaudeUsageData claudeUsage;
    uint16_t        claudeUsageVersion = 0;
    PcStatsData     pcStats;
    uint16_t        pcStatsVersion = 0;

    /* ---- BLE ---- */
    static NimBLEServer* s_server        = nullptr;
    static bool          s_connected     = false;
    static bool          s_wasConnected  = false;
    static bool          s_active        = false;   // advertising desired by the open app

    static volatile bool s_blePending = false;
    static char          s_bleBuf[512];

    class RxCallbacks : public NimBLECharacteristicCallbacks
    {
        void onWrite(NimBLECharacteristic* pChar) override
        {
            NimBLEAttValue val = pChar->getValue();
            size_t len = val.length();
            if (len > 0 && len < sizeof(s_bleBuf))
            {
                memcpy(s_bleBuf, val.c_str(), len);
                s_bleBuf[len] = '\0';
                s_blePending  = true;
            }
        }
    };

    class ServerCallbacks : public NimBLEServerCallbacks
    {
        void onConnect(NimBLEServer*)    override { s_connected = true;  }
        void onDisconnect(NimBLEServer*) override { s_connected = false; }
    };

    /* ---- Parsing (hand-rolled, no JSON library — mirrors wiodeck) ---- */

    static bool parseClaudeUsageJson(const char* json)
    {
        const char* p;

        p = strstr(json, "\"s\":");
        if (!p) return false;
        claudeUsage.session_pct = (float)atoi(p + 4);

        p = strstr(json, "\"sr\":");
        if (!p) return false;
        claudeUsage.session_reset_mins = atoi(p + 5);

        p = strstr(json, "\"w\":");
        if (!p) return false;
        claudeUsage.weekly_pct = (float)atoi(p + 4);

        p = strstr(json, "\"wr\":");
        if (!p) return false;
        claudeUsage.weekly_reset_mins = atoi(p + 5);

        p = strstr(json, "\"st\":\"");
        if (!p) return false;
        p += 6;
        int i = 0;
        while (*p && *p != '"' && i < 15) claudeUsage.status[i++] = *p++;
        claudeUsage.status[i] = '\0';

        p = strstr(json, "\"srt\":\"");
        if (p) {
            p += 7;
            int i2 = 0;
            while (*p && *p != '"' && i2 < 55) claudeUsage.session_reset_str[i2++] = *p++;
            claudeUsage.session_reset_str[i2] = '\0';
        }

        p = strstr(json, "\"wrt\":\"");
        if (p) {
            p += 7;
            int i3 = 0;
            while (*p && *p != '"' && i3 < 55) claudeUsage.weekly_reset_str[i3++] = *p++;
            claudeUsage.weekly_reset_str[i3] = '\0';
        }

        claudeUsage.valid = true;
        claudeUsageVersion++;
        return true;
    }

    static bool parsePcStatsJson(const char* json)
    {
        const char* p;

        p = strstr(json, "\"cpu\":");
        if (!p) return false;
        pcStats.cpu_pct = atoi(p + 6);

        p = strstr(json, "\"ct\":");
        pcStats.cpu_temp = p ? atoi(p + 5) : -1;

        p = strstr(json, "\"ram\":");
        if (!p) return false;
        pcStats.ram_pct = atoi(p + 6);

        p = strstr(json, "\"rb\":\"");
        if (p) {
            p += 6;
            int i = 0;
            while (*p && *p != '"' && i < 19) pcStats.ram_str[i++] = *p++;
            pcStats.ram_str[i] = '\0';
        }

        p = strstr(json, "\"gpu\":");
        pcStats.gpu_pct = p ? atoi(p + 6) : -1;

        p = strstr(json, "\"gt\":");
        pcStats.gpu_temp = p ? atoi(p + 5) : -1;

        p = strstr(json, "\"gn\":\"");
        if (p) {
            p += 6;
            int i = 0;
            while (*p && *p != '"' && i < 31) pcStats.gpu_name[i++] = *p++;
            pcStats.gpu_name[i] = '\0';
        }

        pcStats.valid = true;
        pcStatsVersion++;
        return true;
    }

    static void dispatch(const char* line)
    {
        if (strstr(line, "\"cpu\":"))
            parsePcStatsJson(line);
        else
            parseClaudeUsageJson(line);
    }

    /* ---- Serial ---- */
    static char serialBuf[512];
    static int  serialPos = 0;

    static void checkSerial()
    {
        while (Serial.available())
        {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r')
            {
                if (serialPos > 0)
                {
                    serialBuf[serialPos] = '\0';
                    dispatch(serialBuf);
                    serialPos = 0;
                }
            }
            else if (serialPos < (int)sizeof(serialBuf) - 1)
            {
                serialBuf[serialPos++] = c;
            }
            else
            {
                serialPos = 0;
            }
        }
    }

    static void checkBLE()
    {
        if (s_blePending)
        {
            s_blePending = false;
            dispatch(s_bleBuf);
        }

        /* Restart advertising after a disconnect, only while a screen wants it. */
        if (!s_connected && s_wasConnected && s_active)
        {
            delay(500);
            NimBLEDevice::startAdvertising();
        }
        s_wasConnected = s_connected;
    }

    /* Builds the NimBLE server/service/characteristic + advertising config.
     * Called once at boot from init(), and again from setActive(true) if
     * another app (e.g. BLE Server) has torn the whole NimBLE stack down
     * via NimBLEDevice::deinit() since — mirrors BLE_Volume's
     * !NimBLEDevice::getInitialized() re-init guard. */
    static void _ble_setup()
    {
        NimBLEDevice::init(PC_LINK_DEVICE_NAME);

        s_server = NimBLEDevice::createServer();
        s_server->setCallbacks(new ServerCallbacks());

        NimBLEService* svc = s_server->createService(PC_LINK_SERVICE_UUID);
        NimBLECharacteristic* rxChar = svc->createCharacteristic(
            PC_LINK_RX_CHAR_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        rxChar->setCallbacks(new RxCallbacks());
        svc->start();

        NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
        adv->addServiceUUID(PC_LINK_SERVICE_UUID);
        adv->setScanResponse(true);
        adv->setMinPreferred(0x06);
        adv->setMinPreferred(0x12);

        ESP_LOGI(TAG, "ble setup done, device name %s", PC_LINK_DEVICE_NAME);
    }

    /* ---- Public API ---- */

    void init()
    {
        Serial.begin(115200);
        _ble_setup();
        ESP_LOGI(TAG, "init done");
    }

    void setActive(bool active)
    {
        s_active = active;
        if (active)
        {
            /* Another app (e.g. BLE Server) may have called
             * NimBLEDevice::deinit() since boot, destroying our server/
             * service/characteristic along with the rest of NimBLE.
             * Re-create them before advertising. */
            if (!NimBLEDevice::getInitialized())
            {
                ESP_LOGW(TAG, "NimBLE was deinitialized; re-running ble setup");
                _ble_setup();
            }
            NimBLEDevice::startAdvertising();
        }
        else
        {
            NimBLEDevice::stopAdvertising();
        }
    }

    void poll()
    {
        checkSerial();
        checkBLE();
    }

    const char* bleAddress()
    {
        static std::string addr;
        addr = NimBLEDevice::getAddress().toString();
        return addr.c_str();
    }
}
