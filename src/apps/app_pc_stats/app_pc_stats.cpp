/**
 * @file app_pc_stats.cpp
 * @brief See app_pc_stats.h for the concept and controls.
 */
#include "app_pc_stats.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include "../utilities/pc_link/pc_link.h"


using namespace MOONCAKE::USER_APP;

/* Gauges spread wider apart than a naive even split (CPU/RAM only 8px apart
 * would crowd their labels together). GPU sits alone at the bottom so it
 * gets the biggest radius — it also carries the most text (name + temp). */
static const int CPU_CX = 64,  CPU_CY = 84,  CPU_R = 42;
static const int RAM_CX = 176, RAM_CY = 84,  RAM_R = 42;
static const int GPU_CX = 120, GPU_CY = 188, GPU_R = 48;

/* Blocking button read: 0 = none, 1 = short press, 2 = long press (>700ms). */
static int _read_button(HAL::HAL* hal)
{
    if (hal->encoder.btn.read())
        return 0;
    uint32_t t0 = millis();
    while (!hal->encoder.btn.read())
        delay(5);
    return (millis() - t0 > 700) ? 2 : 1;
}

static void _badge(LGFX_Sprite* c, int cx, int y, const char* label, uint32_t col)
{
    c->setFont(&fonts::Font0);
    c->setTextSize(1);
    int tw = c->textWidth(label);
    int w = tw + 14, h = 15, x = cx - w / 2;
    c->fillRoundRect(x, y, w, h, 7, CYBER::blend(col, CYBER::BG, 0.16f));
    c->drawRoundRect(x, y, w, h, 7, CYBER::blend(col, CYBER::BG, 0.6f));
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(col);
    c->drawString(label, cx, y + h / 2);
}

/* PCB trace board: right-angle traces linking CPU/RAM/GPU like copper traces
 * on a real motherboard, with square via-nodes at every bend/junction —
 * states "one machine" without a straight diagonal line cutting across the
 * gauges' faces. Static (no travelling pulse): this app only redraws when
 * new telemetry arrives, so an animated pulse would just jump discontinuously
 * rather than actually animate. */
static void _draw_circuit_board(LGFX_Sprite* c)
{
    uint32_t traceCol = CYBER::blend(CYBER::SLATE, CYBER::BG, 0.45f);
    uint32_t nodeCol   = CYBER::SLATE;

    auto via = [&](int x, int y) { c->fillRect(x - 1, y - 1, 3, 3, nodeCol); };

    /* CPU - RAM */
    c->drawLine(CPU_CX + CPU_R, CPU_CY, RAM_CX - RAM_R, RAM_CY, traceCol);
    via(CPU_CX + CPU_R, CPU_CY);
    via(RAM_CX - RAM_R, RAM_CY);

    /* RAM - GPU (right-angle route) */
    c->drawLine(RAM_CX, RAM_CY + RAM_R, RAM_CX, 159, traceCol);
    c->drawLine(RAM_CX, 159, 153, 159, traceCol);
    c->drawLine(153, 159, GPU_CX + 35, GPU_CY - 28, traceCol);
    via(RAM_CX, RAM_CY + RAM_R);
    via(RAM_CX, 159);
    via(153, 159);
    via(GPU_CX + 35, GPU_CY - 28);

    /* GPU - CPU (right-angle route) */
    c->drawLine(GPU_CX - 35, GPU_CY - 28, 87, 159, traceCol);
    c->drawLine(87, 159, CPU_CX, 159, traceCol);
    c->drawLine(CPU_CX, 159, CPU_CX, CPU_CY + CPU_R, traceCol);
    via(GPU_CX - 35, GPU_CY - 28);
    via(87, 159);
    via(CPU_CX, 159);
    via(CPU_CX, CPU_CY + CPU_R);
}

/* One gauge: the established segmented pip-ring (progressRingAt), big
 * percentage, label, and — when available — a temp badge or subinfo badge. */
static void _draw_gauge(LGFX_Sprite* c, int cx, int cy, int r, const char* label,
                        int pct, uint32_t col, int temp, const char* subinfo)
{
    float frac = (pct < 0) ? 0.0f : pct / 100.0f;
    CYBER::progressRingAt(c, cx, cy, r, frac, col);

    c->setFont(&fonts::Font0);
    c->setTextDatum(textdatum_t::middle_center);

    c->setTextSize(2);
    if (pct >= 0)
    {
        char v[8]; snprintf(v, sizeof(v), "%d%%", pct);
        c->setTextColor(CYBER::WHITE);
        c->drawString(v, cx, cy - 13);
    }
    else
    {
        c->setTextColor(CYBER::blend(CYBER::SLATE, CYBER::BG, 0.8f));
        c->drawString("N/A", cx, cy - 13);
    }

    c->setTextSize(1);
    c->setTextColor(CYBER::blend(col, CYBER::WHITE, 0.15f));
    c->drawString(label, cx, cy + 7);

    if (temp >= 0)
    {
        char t[8]; snprintf(t, sizeof(t), "%dC", temp);
        uint32_t tcol = (temp < 60) ? CYBER::GREEN : ((temp < 80) ? CYBER::AMBER : CYBER::RED);
        _badge(c, cx, cy + 16, t, tcol);
    }
    else if (subinfo && subinfo[0])
    {
        _badge(c, cx, cy + 16, subinfo, CYBER::SLATE);
    }
}

static void _draw_no_data_panel(LGFX_Sprite* c)
{
    int x = 24, y = 64, w = 192, h = 112;
    c->fillRoundRect(x, y, w, h, 10, CYBER::blend(CYBER::BG, CYBER::WHITE, 0.06f));
    c->drawRoundRect(x, y, w, h, 10, CYBER::blend(CYBER::CYAN, CYBER::BG, 0.4f));

    /* pulsing chip glyph: a small square die with corner pins, standing in
     * for "no telemetry chip connected yet" */
    int ccx = 120, ccy = 86, s = 15;
    uint32_t chipCol = CYBER::blend(CYBER::CYAN, CYBER::BG, 0.85f);
    uint32_t pinCol  = CYBER::blend(CYBER::CYAN, CYBER::BG, 0.6f);
    c->drawRoundRect(ccx - s / 2, ccy - s / 2, s, s, 2, chipCol);
    for (int dx = -1; dx <= 1; dx += 2)
        for (int dyf = 0; dyf < 2; dyf++)
        {
            int dy = dyf == 0 ? -(int)(s * 0.35f) : (int)(s * 0.35f);
            c->drawLine(ccx + dx * s / 2, ccy + dy, ccx + dx * (s / 2 + 4), ccy + dy, pinCol);
        }

    c->setFont(&fonts::Font0);
    c->setTextSize(1);
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(CYBER::WHITE);
    c->drawString("AWAITING DATA", 120, 112);

    c->setTextColor(CYBER::SLATE);
    c->drawString("BLE   M5DIAL-001", 120, 132);
    c->drawString("USB   sysstat_sender.py <port>", 120, 150);

    char addr[48];
    snprintf(addr, sizeof(addr), "ADDR  %s", PC_LINK::bleAddress());
    c->setTextColor(CYBER::blend(CYBER::SLATE, CYBER::BG, 0.8f));
    c->drawString(addr, 120, 168);
}

void PcStats::_render()
{
    LGFX_Sprite* c = _data.hal->canvas;
    c->fillScreen(CYBER::BG);
    for (int y = 0; y < 240; y += 4) c->drawFastHLine(0, y, 240, CYBER::SCANLINE);

    auto& s = PC_LINK::pcStats;

    c->setFont(&fonts::Font0);
    c->setTextSize(1);
    c->setTextDatum(textdatum_t::middle_center);
    c->setTextColor(CYBER::CYAN);
    c->drawString("PC STATS", 108, 20);
    c->fillCircle(163, 20, 3, CYBER::GREEN);

    if (!s.valid)
    {
        _draw_no_data_panel(c);
        _canvas_update();
        return;
    }

    _draw_circuit_board(c);
    _draw_gauge(c, CPU_CX, CPU_CY, CPU_R, "CPU", s.cpu_pct, CYBER::CYAN,    s.cpu_temp, nullptr);
    _draw_gauge(c, RAM_CX, RAM_CY, RAM_R, "RAM", s.ram_pct, CYBER::GREEN,   -1,         s.ram_str);
    _draw_gauge(c, GPU_CX, GPU_CY, GPU_R, "GPU", s.gpu_pct, CYBER::MAGENTA, s.gpu_temp, s.gpu_name);

    _canvas_update();
}

void PcStats::onSetup()
{
    setAppName("PcStats");
    setAllowBgRunning(false);

    PC_STATS::Data_t default_data;
    _data = default_data;
    _data.hal = (HAL::HAL*)getUserData();
}

void PcStats::onCreate()
{
    _log("onCreate");

    PC_LINK::pcStats.valid = false;
    PC_LINK::pcStatsVersion++;

    PC_LINK::setActive(true);

    _data.drawn_version = PC_LINK::pcStatsVersion;
    _data.drawn_valid   = false;
    _data.last_data_ms  = 0;

    _render();
}

void PcStats::onRunning()
{
    PC_LINK::poll();

    if (PC_LINK::pcStats.valid && PC_LINK::pcStatsVersion != _data.drawn_version)
        _data.last_data_ms = millis();

    /* Staleness: revert to the no-data panel if the stream stops for >6s
     * (sysstat_sender.py polls every 2s, so 6s = 3 missed packets). */
    if (PC_LINK::pcStats.valid && _data.last_data_ms &&
        millis() - _data.last_data_ms > 6000)
    {
        PC_LINK::pcStats.valid = false;
        PC_LINK::pcStatsVersion++;
    }

    if (PC_LINK::pcStatsVersion != _data.drawn_version)
    {
        _data.drawn_version = PC_LINK::pcStatsVersion;
        _data.drawn_valid   = PC_LINK::pcStats.valid;
        _render();
    }

    if (_read_button(_data.hal) == 2)
    {
        destroyApp();
        return;
    }

    delay(20);
}

void PcStats::onDestroy()
{
    _log("onDestroy");
    PC_LINK::setActive(false);
}
