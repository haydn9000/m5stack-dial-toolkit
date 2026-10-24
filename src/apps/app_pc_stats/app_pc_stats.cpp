/**
 * @file app_pc_stats.cpp
 * @brief See app_pc_stats.h for the concept and controls.
 */
#include "app_pc_stats.h"
#include "../common_define.h"
#include "../utilities/cyber_ui/cyber_ui.hpp"
#include "../utilities/pc_link/pc_link.h"


using namespace MOONCAKE::USER_APP;

/* Three stacked rows (CPU / RAM / GPU): label left, percentage right, a bar,
 * then temperature / detail under the bar. Rows read far better on the
 * round panel than three small rings did: the text gets the full width of
 * the screen instead of the inside of a 42px gauge. */
static const int ROW_X0 = 42, ROW_X1 = 198;          // usable width at the rows' heights
static const int ROW_Y[3] = {60, 112, 164};

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

static uint32_t _temp_color(int t)
{
    return (t < 60) ? CYBER::GREEN : ((t < 80) ? CYBER::AMBER : CYBER::RED);
}

/* "61°C" — the DejaVu fonts are ASCII-only, so the degree sign is drawn. */
static void _draw_temp(LGFX_Sprite* c, int x, int y, int temp)
{
    char t[8];
    snprintf(t, sizeof(t), "%d", temp);
    uint32_t col = _temp_color(temp);
    CYBER::text(c, t, x, y, CYBER::FONT_SMALL(), col, textdatum_t::middle_left);
    int w = c->textWidth(t);
    c->drawCircle(x + w + 3, y - 4, 2, col);
    CYBER::text(c, "C", x + w + 7, y, CYBER::FONT_SMALL(), col, textdatum_t::middle_left);
}

/* "NVIDIA GeForce RTX 4070" -> "RTX 4070": the vendor words don't fit and
 * don't help. */
static const char* _short_gpu(const char* name)
{
    static const char* PREFIX[] = {"NVIDIA ", "GeForce ", "AMD ", "Radeon ", "Intel(R) ", "Intel "};
    bool again = true;
    while (again)
    {
        again = false;
        for (const char* p : PREFIX)
        {
            size_t n = strlen(p);
            if (strncmp(name, p, n) == 0) { name += n; again = true; }
        }
    }
    return name;
}

static void _draw_row(LGFX_Sprite* c, int y, const char* label, int pct, uint32_t col,
                      int temp, const char* subinfo)
{
    CYBER::text(c, label, ROW_X0, y, CYBER::FONT_LABEL(), col, textdatum_t::middle_left);

    int sy = y + 29;
    if (temp >= 0)
        _draw_temp(c, ROW_X0, sy, temp);
    if (subinfo && subinfo[0])
        CYBER::text(c, subinfo, ROW_X1, sy, CYBER::FONT_SMALL(), CYBER::SLATE, textdatum_t::middle_right);

    char v[8];
    if (pct >= 0) snprintf(v, sizeof(v), "%d%%", pct);
    else          snprintf(v, sizeof(v), "N/A");
    CYBER::text(c, v, ROW_X1, y, CYBER::FONT_VALUE(), pct >= 0 ? CYBER::WHITE : CYBER::SLATE,
                textdatum_t::middle_right);

    /* Bar: dim track, accent fill, bright leading tick. */
    int bw = ROW_X1 - ROW_X0, by = y + 14, bh = 5;
    c->fillRoundRect(ROW_X0, by, bw, bh, bh / 2, CYBER::blend(CYBER::TRACK, CYBER::BG, 0.9f));
    if (pct > 0)
    {
        int fw = bw * (pct > 100 ? 100 : pct) / 100;
        if (fw < bh) fw = bh;
        c->fillRoundRect(ROW_X0, by, fw, bh, bh / 2, col);
        c->fillRect(ROW_X0 + fw - 2, by, 2, bh, CYBER::WHITE);
    }
}

/* Small chip glyph: a square die with pins, for the waiting screen. */
static void _chip_glyph(LGFX_Sprite* c, int cx, int cy, int s, uint32_t col)
{
    c->drawRoundRect(cx - s / 2, cy - s / 2, s, s, 3, col);
    c->drawRoundRect(cx - s / 2 + 5, cy - s / 2 + 5, s - 10, s - 10, 2, CYBER::blend(col, CYBER::BG, 0.6f));
    for (int k = -1; k <= 1; k++)
    {
        int o = k * s / 4;
        c->drawFastHLine(cx - s / 2 - 5, cy + o, 5, col);
        c->drawFastHLine(cx + s / 2, cy + o, 5, col);
        c->drawFastVLine(cx + o, cy - s / 2 - 5, 5, col);
        c->drawFastVLine(cx + o, cy + s / 2, 5, col);
    }
}

static void _draw_no_data_panel(LGFX_Sprite* c)
{
    _chip_glyph(c, 120, 62, 26, CYBER::CYAN);

    CYBER::text(c, "PC stats", 120, 102, &fonts::Font4, CYBER::WHITE);
    CYBER::text(c, "waiting for data...", 120, 128, CYBER::FONT_SMALL(), CYBER::blend(CYBER::CYAN, CYBER::WHITE, 0.4f));

    c->drawFastHLine(60, 146, 120, CYBER::blend(CYBER::CYAN, CYBER::BG, 0.35f));

    CYBER::text(c, "BLE  M5DIAL-001", 120, 162, &fonts::Font2, CYBER::SLATE);
    CYBER::text(c, "or USB: sysstat_sender.py", 120, 180, &fonts::Font2, CYBER::SLATE);
    CYBER::text(c, PC_LINK::bleAddress(), 120, 198, &fonts::Font0, CYBER::DIMTEXT);
}

void PcStats::_render()
{
    LGFX_Sprite* c = _data.hal->canvas;
    c->fillScreen(CYBER::BG);

    auto& s = PC_LINK::pcStats;

    if (!s.valid)
    {
        _draw_no_data_panel(c);
        _canvas_update();
        return;
    }

    CYBER::text(c, "PC stats", 120, 30, CYBER::FONT_LABEL(), CYBER::blend(CYBER::CYAN, CYBER::WHITE, 0.75f));

    _draw_row(c, ROW_Y[0], "CPU", s.cpu_pct, CYBER::CYAN,    s.cpu_temp, nullptr);
    _draw_row(c, ROW_Y[1], "RAM", s.ram_pct, CYBER::GREEN,   -1,         s.ram_str);
    _draw_row(c, ROW_Y[2], "GPU", s.gpu_pct, CYBER::MAGENTA, s.gpu_temp, _short_gpu(s.gpu_name));

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
