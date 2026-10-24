/**
 * @file app_time_sync.cpp
 * @brief See app_time_sync.h for the concept and controls.
 */
#include "app_time_sync.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include "../utilities/time_sync/time_sync.h"


using namespace MOONCAKE::USER_APP;

static const char* ACTION_LABEL[TIME_SYNC_APP::A_COUNT] = {"Sync now", "Set up Wi-Fi"};

/* Blocking button read: 0 = none, 1 = short press, 2 = long press (>700ms).
 * Same idiom as the other apps. */
static int _read_button(HAL::HAL* hal)
{
    if (hal->encoder.btn.read())
        return 0;
    uint32_t t0 = millis();
    while (!hal->encoder.btn.read())
        delay(5);
    return (millis() - t0 > 700) ? 2 : 1;
}

static int _read_detents(HAL::HAL* hal, int64_t& last_raw)
{
    int64_t raw   = hal->encoder.getCount();
    int64_t delta = raw - last_raw;
    if (delta <= -2 || delta >= 2)
    {
        int detents = (int)(delta / 2);
        last_raw += (int64_t)detents * 2;
        return detents;
    }
    return 0;
}

/* "4m 12s slow" / "on time" — drift is RTC minus internet time. */
static void _fmt_drift(char* buf, size_t n, int32_t d)
{
    int32_t a = d < 0 ? -d : d;
    const char* dir = d < 0 ? "slow" : "fast";
    if (a < 2)          snprintf(buf, n, "Clock was on time");
    else if (a < 60)    snprintf(buf, n, "Clock was %lds %s", (long)a, dir);
    else if (a < 3600)  snprintf(buf, n, "Clock was %ldm %lds %s", (long)(a / 60), (long)(a % 60), dir);
    else                snprintf(buf, n, "Clock was %ldh %ldm %s", (long)(a / 3600), (long)(a % 3600 / 60), dir);
}


void TimeSyncApp::_render_main()
{
    LGFX_Sprite* c = _data.hal->canvas;
    TIME_SYNC::State st = TIME_SYNC::state();

    uint32_t accent;
    char state[32];
    bool live = false;
    switch (st)
    {
        case TIME_SYNC::CONNECTING: accent = CYBER::CYAN;  snprintf(state, sizeof(state), "Joining Wi-Fi"); live = true; break;
        case TIME_SYNC::SYNCING:    accent = CYBER::CYAN;  snprintf(state, sizeof(state), "Asking time server"); live = true; break;
        case TIME_SYNC::FAILED:     accent = CYBER::RED;   snprintf(state, sizeof(state), "Sync failed"); break;
        case TIME_SYNC::NOT_SET_UP: accent = CYBER::AMBER; snprintf(state, sizeof(state), "Wi-Fi not set up"); break;
        default:
            accent = CYBER::GREEN;
            TIME_SYNC::statusLine(state, sizeof(state));
            break;
    }

    uint32_t bootMs = millis() - _data.boot_start;
    CYBER::hudChrome(c, "Internet time", state, accent, bootMs, live);

    /* Ring = how far we are through the interval until the next auto-sync. */
    int32_t age = TIME_SYNC::secondsSinceSync();
    float ring = age < 0 ? 0.0f : 1.0f - (float)age / (TIME_SYNC::SYNC_INTERVAL_H * 3600.0f);
    if (ring < 0) ring = 0;
    CYBER::progressRing(c, ring * CYBER::bootProgress(bootMs), accent);

    tm now;
    char big[8] = "--:--";
    if (_data.hal->rtc.getTime(now) == ESP_OK)
        snprintf(big, sizeof(big), "%02d:%02d", now.tm_hour, now.tm_min);
    CYBER::bigTime(c, big, CYBER::WHITE);

    /* Context: the network (or what went wrong), then the measured drift. */
    char line[40];
    if (st == TIME_SYNC::FAILED)
        CYBER::text(c, TIME_SYNC::lastError(), CYBER::CX, 152, CYBER::FONT_SMALL(),
                    CYBER::blend(CYBER::RED, CYBER::WHITE, 0.6f));
    else if (TIME_SYNC::hasCredentials())
    {
        snprintf(line, sizeof(line), "Wi-Fi: %s", TIME_SYNC::ssid());
        CYBER::text(c, line, CYBER::CX, 152, CYBER::FONT_SMALL(), CYBER::SLATE);
    }
    else
        CYBER::text(c, "Set up from your phone", CYBER::CX, 152, CYBER::FONT_SMALL(), CYBER::SLATE);

    /* What the last sync found: a power loss beats everything (that's the
     * likely cause of a clock that "falls behind"), then the steady rate,
     * then the one-off correction. */
    int32_t drift;
    float rate;
    if (TIME_SYNC::rtcHadStopped())
        CYBER::text(c, "Clock had lost power", CYBER::CX, 168, CYBER::FONT_SMALL(), CYBER::AMBER);
    else if (TIME_SYNC::driftRate(rate))
    {
        float a = rate < 0 ? -rate : rate;
        snprintf(line, sizeof(line), "Runs %.1f s/day %s", a, rate < 0 ? "slow" : "fast");
        CYBER::text(c, line, CYBER::CX, 168, CYBER::FONT_SMALL(), CYBER::blend(CYBER::SLATE, CYBER::BG, 0.75f));
    }
    else if (TIME_SYNC::lastDrift(drift))
    {
        _fmt_drift(line, sizeof(line), drift);
        CYBER::text(c, line, CYBER::CX, 168, CYBER::FONT_SMALL(), CYBER::blend(CYBER::SLATE, CYBER::BG, 0.75f));
    }

    /* The selected action, with arrows showing the dial can change it. */
    snprintf(line, sizeof(line), "< %s >", ACTION_LABEL[_data.action]);
    CYBER::text(c, line, CYBER::CX, 188, CYBER::FONT_LABEL(), CYBER::blend(CYBER::AMBER, CYBER::WHITE, 0.8f));
    CYBER::text(c, "Hold to exit", CYBER::CX, 210, CYBER::FONT_SMALL(), CYBER::blend(CYBER::SLATE, CYBER::BG, 0.6f));

    _canvas_update();
}

void TimeSyncApp::_render_portal()
{
    LGFX_Sprite* c = _data.hal->canvas;
    CYBER::background(c);

    if (_data.saved_at)
    {
        CYBER::text(c, "Saved", CYBER::CX, 104, CYBER::FONT_VALUE(), CYBER::GREEN);
        CYBER::text(c, "Joining your Wi-Fi...", CYBER::CX, 136, CYBER::FONT_SMALL(), CYBER::SLATE);
        _canvas_update();
        return;
    }

    CYBER::text(c, "Set up Wi-Fi", CYBER::CX, 34, CYBER::FONT_LABEL(), CYBER::CYAN);

    /* QR that joins the dial's open setup network. White quiet zone first —
     * phone cameras need the light border to find the code. */
    char qr[64];
    snprintf(qr, sizeof(qr), "WIFI:T:nopass;S:%s;;", TIME_SYNC::AP_SSID);
    c->fillRoundRect(64, 50, 112, 112, 6, TFT_WHITE);
    c->qrcode(qr, 68, 54, 104, 3);

    CYBER::text(c, "Scan to join, or pick", CYBER::CX, 176, CYBER::FONT_SMALL(), CYBER::WHITE);
    CYBER::text(c, TIME_SYNC::AP_SSID, CYBER::CX, 192, CYBER::FONT_SMALL(), CYBER::CYAN);
    CYBER::text(c, "Hold to cancel", CYBER::CX, 210, CYBER::FONT_SMALL(), CYBER::blend(CYBER::SLATE, CYBER::BG, 0.6f));
    _canvas_update();
}


void TimeSyncApp::onSetup()
{
    setAppName("TimeSync");
    setAllowBgRunning(false);

    TIME_SYNC_APP::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}

void TimeSyncApp::onCreate()
{
    _log("onCreate");
    _data.last_raw   = _data.hal->encoder.getCount();
    _data.boot_start = millis();
    _data.action     = TIME_SYNC::hasCredentials() ? TIME_SYNC_APP::A_SYNC : TIME_SYNC_APP::A_SETUP;
    _render_main();
}

void TimeSyncApp::onRunning()
{
    if (_data.portal)
    {
        bool saved = TIME_SYNC::portalPoll();
        if (saved && !_data.saved_at)
        {
            _data.saved_at = millis();
            _data.hal->buzz.fxComplete();
            _render_portal();
        }
        /* Give the phone a moment to receive the "Saved" page, then hand
         * the radio back so the sync task can join the new network. */
        if (_data.saved_at && millis() - _data.saved_at > 2500)
        {
            TIME_SYNC::stopPortal();
            _data.portal   = false;
            _data.saved_at = 0;
            _data.action   = TIME_SYNC_APP::A_SYNC;
            _render_main();
        }
        if (_read_button(_data.hal) == 2)
        {
            TIME_SYNC::stopPortal();
            _data.portal   = false;
            _data.saved_at = 0;
            _data.hal->buzz.fxCancel();
            _render_main();
        }
        delay(2);
        return;
    }

    int det = _read_detents(_data.hal, _data.last_raw);
    if (det != 0)
    {
        int n = TIME_SYNC_APP::A_COUNT;
        _data.action = ((_data.action + det) % n + n) % n;
        _render_main();
    }

    int btn = _read_button(_data.hal);
    if (btn == 2)
    {
        destroyApp();
        return;
    }
    if (btn == 1)
    {
        if (_data.action == TIME_SYNC_APP::A_SETUP)
        {
            _data.portal = true;
            _data.hal->buzz.fxConfirm();
            /* Show the screen first: bringing the access point up takes a
             * moment and the dial should look like it responded. */
            _render_portal();
            TIME_SYNC::startPortal();
            return;
        }
        else if (TIME_SYNC::hasCredentials())
        {
            TIME_SYNC::requestSync();
            _data.hal->buzz.fxConfirm();
        }
        else
            _data.hal->buzz.fxCancel();
    }

    /* Status changes come from the background task, so refresh steadily. */
    if (millis() - _data.last_render >= 250 ||
        (CYBER::booting(millis() - _data.boot_start) && millis() - _data.last_render >= 33))
    {
        _data.last_render = millis();
        _render_main();
    }
    delay(4);
}

void TimeSyncApp::onDestroy()
{
    _log("onDestroy");
    TIME_SYNC::stopPortal();
}
