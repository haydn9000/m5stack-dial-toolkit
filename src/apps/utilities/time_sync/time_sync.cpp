/**
 * @file time_sync.cpp
 * @brief See time_sync.h.
 */
#include "time_sync.h"
#include "../../../hal/hal.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

static const char* TAG = "time_sync";

namespace TIME_SYNC
{
    /* Eastern time — the RTC was set by hand in that zone before this module
     * existed, so it's the least surprising default until the user picks. */
    static const char* DEFAULT_TZ = "EST5EDT,M3.2.0,M11.1.0";

    static HAL::HAL*         _hal   = nullptr;
    static TaskHandle_t      _task  = nullptr;
    static SemaphoreHandle_t _radio = nullptr;

    static volatile State _state = NOT_SET_UP;
    static char  _ssid[33] = "";
    static char  _pass[65] = "";
    static char  _tz[64]   = "";
    static char  _err[40]  = "";

    static volatile bool     _synced  = false;
    static volatile uint32_t _sync_ms = 0;
    static int32_t           _drift   = 0;
    static bool              _has_drift = false;
    static float             _rate      = 0.0f;    // s/day, RTC minus real
    static bool              _has_rate  = false;
    static bool              _stopped   = false;   // RTC VL flag at last sync
    static volatile bool     _sntp_done = false;


    /* ---------------- helpers ---------------- */

    /* Seconds since 1970 for a broken-down time read as UTC (days-from-civil).
     * Only used for differences, so the zone doesn't matter. */
    static int64_t _secs(int year, int mon /*0-11*/, int mday, int h, int m, int s)
    {
        int y = year - (mon < 2 ? 1 : 0);
        int mm = mon + 1;
        int era = (y >= 0 ? y : y - 399) / 400;
        int yoe = y - era * 400;
        int doy = (153 * (mm + (mm > 2 ? -3 : 9)) + 2) / 5 + mday - 1;
        int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        int64_t days = (int64_t)era * 146097 + doe - 719468;
        return days * 86400 + h * 3600 + m * 60 + s;
    }

    static void _load()
    {
        Preferences p;
        p.begin("settings", true);
        if (p.isKey("wifi_ssid")) p.getString("wifi_ssid", _ssid, sizeof(_ssid));
        if (p.isKey("wifi_pass")) p.getString("wifi_pass", _pass, sizeof(_pass));
        if (p.isKey("tz"))        p.getString("tz", _tz, sizeof(_tz));
        _has_drift = p.isKey("drift");
        _drift     = p.getInt("drift", 0);
        _has_rate  = p.isKey("drift_rate");
        _rate      = p.getFloat("drift_rate", 0.0f);
        _stopped   = p.getBool("rtc_stopped", false);
        p.end();
        if (_tz[0] == '\0')
            strlcpy(_tz, DEFAULT_TZ, sizeof(_tz));
    }

    static void _radio_off()
    {
        /* One clean shutdown: disconnect(true) already switches the mode off,
         * and a second WIFI_OFF on top logs "timeout when WiFi un-init". */
        if (WiFi.getMode() == WIFI_OFF) return;
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
    }

    static void _on_sntp(struct timeval*) { _sntp_done = true; }

    static void _fail(const char* why)
    {
        strlcpy(_err, why, sizeof(_err));
        ESP_LOGW(TAG, "sync failed: %s", why);
        _state = FAILED;
    }


    /* ---------------- the sync itself ---------------- */

    static bool _do_sync()
    {
        RadioLock lock;
        _state = CONNECTING;

        WiFi.persistent(false);   // credentials live in our own NVS keys
        WiFi.mode(WIFI_STA);
        WiFi.begin(_ssid, _pass);

        uint32_t t0 = millis();
        wl_status_t st = WiFi.status();
        while (st != WL_CONNECTED && millis() - t0 < 20000)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            st = WiFi.status();
            /* These are final verdicts from the driver — no point waiting. */
            if ((st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED) && millis() - t0 > 6000)
                break;
        }
        if (st != WL_CONNECTED)
        {
            _radio_off();
            _fail(st == WL_NO_SSID_AVAIL   ? "Network not found"
                  : st == WL_CONNECT_FAILED ? "Wrong Wi-Fi password?"
                                            : "Couldn't join Wi-Fi");
            return false;
        }

        _state     = SYNCING;
        _sntp_done = false;
        sntp_set_time_sync_notification_cb(_on_sntp);
        configTzTime(_tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

        t0 = millis();
        while (!_sntp_done && millis() - t0 < 15000)
            vTaskDelay(pdMS_TO_TICKS(100));
        sntp_stop();
        if (!_sntp_done)
        {
            _radio_off();
            _fail("No reply from time server");
            return false;
        }

        /* Write the RTC exactly on a second boundary so it starts in phase. */
        timeval tv;
        gettimeofday(&tv, nullptr);
        vTaskDelay(pdMS_TO_TICKS((1000000 - tv.tv_usec) / 1000));
        time_t now = time(nullptr);
        tm lt;
        localtime_r(&now, &lt);

        tm before = {};
        bool had_rtc = (_hal->rtc.getTime(before) == ESP_OK);
        /* VL set = the RTC's oscillator stopped at some point (its supply
         * dropped too low), so its time froze and resumed late. That reads
         * as "falling behind" but isn't drift — record it separately. */
        _stopped = had_rtc && _hal->rtc.lowVoltage();

        tm w = lt;
        w.tm_year += 1900;        // PCF8563::setTime takes the full year
        _hal->rtc.setTime(w);

        int64_t now_s = _secs(w.tm_year, w.tm_mon, w.tm_mday, w.tm_hour, w.tm_min, w.tm_sec);
        Preferences p;
        p.begin("settings", false);
        if (had_rtc)
        {
            int64_t d = _secs(before.tm_year, before.tm_mon, before.tm_mday,
                              before.tm_hour, before.tm_min, before.tm_sec) - now_s;
            /* Ignore nonsense from an RTC that had lost its time entirely. */
            if (d > -86400 * 30 && d < 86400 * 30)
            {
                _drift = (int32_t)d;
                _has_drift = true;
                p.putInt("drift", _drift);

                /* Rate since the previous sync — only meaningful if the RTC
                 * ran untouched in between (Set Time clears "sync_at") and
                 * long enough for whole-second readings to resolve it. */
                int64_t prev = p.getLong64("sync_at", 0);
                if (!_stopped && prev > 0 && now_s - prev > 3 * 3600)
                {
                    _rate = (float)((double)d * 86400.0 / (double)(now_s - prev));
                    _has_rate = true;
                    p.putFloat("drift_rate", _rate);
                }
            }
        }
        p.putLong64("sync_at", now_s);
        p.putBool("rtc_stopped", _stopped);
        p.end();

        _radio_off();
        ESP_LOGI(TAG, "RTC set to %04d-%02d-%02d %02d:%02d:%02d (drift %ld s)",
                 w.tm_year, w.tm_mon + 1, w.tm_mday, w.tm_hour, w.tm_min, w.tm_sec,
                 (long)_drift);
        _err[0]  = '\0';
        _synced  = true;
        _sync_ms = millis();
        _state   = SYNCED;
        return true;
    }

    static void _task_fn(void*)
    {
        TickType_t wait = pdMS_TO_TICKS(3000);   // let the UI come up first
        for (;;)
        {
            ulTaskNotifyTake(pdTRUE, wait);

            if (_state == PORTAL)
            {
                wait = pdMS_TO_TICKS(60000);
                continue;
            }
            if (_ssid[0] == '\0')
            {
                _state = NOT_SET_UP;
                wait = portMAX_DELAY;
                continue;
            }

            bool ok = _do_sync();
            wait = ok ? (TickType_t)SYNC_INTERVAL_H * 3600 * configTICK_RATE_HZ
                      : (TickType_t)RETRY_MIN * 60 * configTICK_RATE_HZ;
        }
    }


    /* ---------------- public API ---------------- */

    RadioLock::RadioLock()  { if (_radio) xSemaphoreTake(_radio, portMAX_DELAY); }
    RadioLock::~RadioLock() { if (_radio) xSemaphoreGive(_radio); }

    void radioIdle()
    {
        State s = _state;
        if (s != CONNECTING && s != SYNCING && s != PORTAL)
            _radio_off();
    }

    void begin(HAL::HAL* hal)
    {
        _hal   = hal;
        _radio = xSemaphoreCreateMutex();
        _load();
        _state = _ssid[0] ? IDLE : NOT_SET_UP;
        xTaskCreatePinnedToCore(_task_fn, "time_sync", 6144, nullptr, 1, &_task, 0);
    }

    State       state()          { return _state; }
    const char* lastError()      { return _err; }
    const char* ssid()           { return _ssid; }
    bool        hasCredentials() { return _ssid[0] != '\0'; }

    void requestSync()
    {
        if (_task) xTaskNotifyGive(_task);
    }

    int32_t secondsSinceSync()
    {
        return _synced ? (int32_t)((millis() - _sync_ms) / 1000) : -1;
    }

    bool lastDrift(int32_t& driftSec)
    {
        driftSec = _drift;
        return _has_drift;
    }

    bool driftRate(float& secPerDay)
    {
        secPerDay = _rate;
        return _has_rate;
    }

    bool rtcHadStopped() { return _stopped; }

    void forgetSyncPoint()
    {
        Preferences p;
        p.begin("settings", false);
        p.remove("sync_at");
        p.end();
    }

    bool statusLine(char* buf, size_t n)
    {
        State s = _state;
        if (s == PORTAL)                     { snprintf(buf, n, "Wi-Fi setup open"); return false; }
        if (s == NOT_SET_UP)                 { snprintf(buf, n, "Wi-Fi time not set up"); return false; }
        if (s == CONNECTING || s == SYNCING) { snprintf(buf, n, "Syncing time..."); return false; }

        int32_t age = secondsSinceSync();
        if (age < 0)
        {
            snprintf(buf, n, s == FAILED ? "Time sync failed" : "Not synced yet");
            return false;
        }
        if (age < 120)        snprintf(buf, n, "Synced just now");
        else if (age < 3600)  snprintf(buf, n, "Synced %ldm ago", (long)(age / 60));
        else                  snprintf(buf, n, "Synced %ldh ago", (long)(age / 3600));
        return age < 86400;
    }

    void saveCredentials(const char* ssid, const char* pass, const char* tz)
    {
        strlcpy(_ssid, ssid, sizeof(_ssid));
        strlcpy(_pass, pass, sizeof(_pass));
        if (tz && tz[0]) strlcpy(_tz, tz, sizeof(_tz));
        Preferences p;
        p.begin("settings", false);
        p.putString("wifi_ssid", _ssid);
        p.putString("wifi_pass", _pass);
        p.putString("tz", _tz);
        p.end();
    }


    /* ---------------- setup portal ---------------- */

    static WebServer* _web = nullptr;
    static DNSServer* _dns = nullptr;
    static bool       _saved = false;

    struct Zone { const char* name; const char* iana; const char* posix; };
    static const Zone ZONES[] = {
        {"US Eastern",        "America/New_York",    "EST5EDT,M3.2.0,M11.1.0"},
        {"US Central",        "America/Chicago",     "CST6CDT,M3.2.0,M11.1.0"},
        {"US Mountain",       "America/Denver",      "MST7MDT,M3.2.0,M11.1.0"},
        {"US Arizona",        "America/Phoenix",     "MST7"},
        {"US Pacific",        "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
        {"US Alaska",         "America/Anchorage",   "AKST9AKDT,M3.2.0,M11.1.0"},
        {"Hawaii",            "Pacific/Honolulu",    "HST10"},
        {"Atlantic (Canada)", "America/Halifax",     "AST4ADT,M3.2.0,M11.1.0"},
        {"Brazil (Brasilia)", "America/Sao_Paulo",   "<-03>3"},
        {"UK / Ireland",      "Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0"},
        {"Central Europe",    "Europe/Paris",        "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Eastern Europe",    "Europe/Athens",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Moscow",            "Europe/Moscow",       "MSK-3"},
        {"Gulf",              "Asia/Dubai",          "<+04>-4"},
        {"India",             "Asia/Kolkata",        "IST-5:30"},
        {"China",             "Asia/Shanghai",       "CST-8"},
        {"Japan",             "Asia/Tokyo",          "JST-9"},
        {"Sydney",            "Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"New Zealand",       "Pacific/Auckland",    "NZST-12NZDT,M9.5.0,M4.1.0/3"},
        {"UTC",               "UTC",                 "UTC0"},
    };

    static String _esc(const String& s)
    {
        String o;
        for (size_t i = 0; i < s.length(); i++)
        {
            char ch = s[i];
            if (ch == '&')      o += "&amp;";
            else if (ch == '<') o += "&lt;";
            else if (ch == '>') o += "&gt;";
            else if (ch == '"') o += "&quot;";
            else                o += ch;
        }
        return o;
    }

    static const char PAGE_HEAD[] PROGMEM =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>M5Dial Wi-Fi</title><style>"
        "body{margin:0;background:#070b12;color:#eaf6ff;font:16px system-ui,sans-serif}"
        "main{max-width:420px;margin:0 auto;padding:24px 16px}"
        "h1{font-size:22px;margin:0 0 4px;color:#00f0ff}p{color:#8fb0c2;margin:0 0 20px}"
        "label{display:block;margin:16px 0 6px;color:#8fb0c2;font-size:14px}"
        "input,select{width:100%;box-sizing:border-box;padding:12px;font-size:16px;border-radius:8px;"
        "border:1px solid #1f4a5e;background:#0d1622;color:#eaf6ff}"
        "button{margin-top:24px;width:100%;padding:14px;font-size:17px;font-weight:600;border:0;"
        "border-radius:8px;background:#00f0ff;color:#070b12}"
        "</style></head><body><main>";

    static void _page_root()
    {
        String h = FPSTR(PAGE_HEAD);
        h += "<h1>Connect your dial</h1><p>The dial uses Wi-Fi only to set its clock from the internet.</p>"
             "<form method=post action=/save>"
             "<label for=s>Wi-Fi network</label><input id=s name=ssid list=nets required value=\"";
        h += _esc(_ssid);
        h += "\"><datalist id=nets>";
        int n = WiFi.scanComplete();
        for (int i = 0; i < n; i++)
            h += "<option value=\"" + _esc(WiFi.SSID(i)) + "\">";
        h += "</datalist>"
             "<label for=p>Password</label><input id=p name=pass type=password autocomplete=off "
             "placeholder='Leave empty for an open network'>"
             "<label for=z>Time zone</label><select id=z name=tz>";
        for (const Zone& z : ZONES)
        {
            h += "<option data-iana=\"";
            h += z.iana;
            h += "\" value=\"";
            h += z.posix;
            h += "\"";
            if (strcmp(z.posix, _tz) == 0) h += " selected";
            h += ">";
            h += z.name;
            h += "</option>";
        }
        h += "</select><button>Save and set the clock</button></form>"
             "<script>try{var t=Intl.DateTimeFormat().resolvedOptions().timeZone;"
             "for(var o of document.querySelectorAll('#z option'))if(o.dataset.iana==t)o.selected=true}catch(e){}"
             "</script></main></body></html>";
        _web->send(200, "text/html", h);
    }

    static void _page_save()
    {
        String ssid = _web->arg("ssid");
        ssid.trim();
        if (ssid.length() == 0 || ssid.length() > 32)
        {
            _web->sendHeader("Location", "/", true);
            _web->send(302, "text/plain", "");
            return;
        }
        saveCredentials(ssid.c_str(), _web->arg("pass").c_str(), _web->arg("tz").c_str());
        String h = FPSTR(PAGE_HEAD);
        h += "<h1>Saved</h1><p>The dial will close this network, join <b>";
        h += _esc(ssid);
        h += "</b> and set its clock. You can reconnect your phone to your usual Wi-Fi.</p></main></body></html>";
        _web->send(200, "text/html", h);
        _saved = true;
    }

    static void _page_redirect()
    {
        /* Every unknown URL (including the OS captive-portal probes) lands on
         * the setup page, which makes phones pop it up on their own. */
        _web->sendHeader("Location", "http://192.168.4.1/", true);
        _web->send(302, "text/plain", "");
    }

    void startPortal()
    {
        if (_state == PORTAL) return;
        if (_radio) xSemaphoreTake(_radio, pdMS_TO_TICKS(40000));
        _state = PORTAL;
        _saved = false;

        WiFi.persistent(false);
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID);
        WiFi.scanNetworks(true);   // async — fills the network list on the page

        _dns = new DNSServer();
        _dns->start(53, "*", WiFi.softAPIP());
        _web = new WebServer(80);
        _web->on("/", HTTP_GET, _page_root);
        _web->on("/save", HTTP_POST, _page_save);
        _web->onNotFound(_page_redirect);
        _web->begin();
        ESP_LOGI(TAG, "portal up at %s", WiFi.softAPIP().toString().c_str());
    }

    bool portalPoll()
    {
        if (_state != PORTAL) return false;
        _dns->processNextRequest();
        _web->handleClient();
        return _saved;
    }

    void stopPortal()
    {
        if (_state != PORTAL) return;
        _web->stop();
        delete _web;
        _web = nullptr;
        _dns->stop();
        delete _dns;
        _dns = nullptr;
        WiFi.scanDelete();
        WiFi.softAPdisconnect(true);
        _radio_off();
        _state = _ssid[0] ? IDLE : NOT_SET_UP;
        if (_radio) xSemaphoreGive(_radio);
        if (_saved) requestSync();
    }
}
