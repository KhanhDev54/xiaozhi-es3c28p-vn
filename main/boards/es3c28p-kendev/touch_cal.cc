#include "touch_cal.h"

#include <cmath>
#include <esp_log.h>

#include "config.h"
#include "settings.h"

#define TAG "TouchCal"

namespace touch_cal {

static Mapping s_map;
static const float kMargin = 30.0f;

void Load() {
    Settings s("touch", false);
    s_map.valid = s.GetInt("valid", 0) != 0;
    if (!s_map.valid) {
        // Best guess until the user calibrates: raw portrait 240x320 rotated to landscape
        s_map.swap = true;
        s_map.ax = kMargin;
        s_map.ay = DISPLAY_HEIGHT - 1 - kMargin;
        s_map.sx = 1;
        s_map.sy = -1;
        return;
    }
    s_map.swap = s.GetInt("swap", 1) != 0;
    s_map.ax = s.GetInt("ax", 0) / 100.0f;
    s_map.ay = s.GetInt("ay", 0) / 100.0f;
    s_map.sx = s.GetInt("sx", 100) / 1000.0f;
    s_map.sy = s.GetInt("sy", 100) / 1000.0f;
    ESP_LOGI(TAG, "Loaded: swap=%d a=(%.1f,%.1f) s=(%.3f,%.3f)", s_map.swap, s_map.ax, s_map.ay, s_map.sx, s_map.sy);
}

bool IsCalibrated() { return s_map.valid; }

void Reset() {
    Settings s("touch", true);
    s.SetInt("valid", 0);
    Load();
}

bool Solve(uint16_t r1x, uint16_t r1y, uint16_t r2x, uint16_t r2y, int width, int height) {
    float dxs = width - 2 * kMargin, dys = height - 2 * kMargin;
    // Hypothesis A: no swap (raw x -> screen x). B: swap.
    float a_sx = (r2x - (float)r1x) / dxs, a_sy = (r2y - (float)r1y) / dys;
    float b_sx = (r2y - (float)r1y) / dxs, b_sy = (r2x - (float)r1x) / dys;
    auto err = [](float a, float b) { return std::fabs(std::fabs(a) - 1.0f) + std::fabs(std::fabs(b) - 1.0f); };
    bool swap = err(b_sx, b_sy) < err(a_sx, a_sy);
    Mapping m;
    m.swap = swap;
    m.sx = swap ? b_sx : a_sx;
    m.sy = swap ? b_sy : a_sy;
    m.ax = swap ? r1y : r1x;
    m.ay = swap ? r1x : r1y;
    if (std::fabs(m.sx) < 0.3f || std::fabs(m.sy) < 0.3f || std::fabs(m.sx) > 3 || std::fabs(m.sy) > 3) {
        ESP_LOGW(TAG, "Calibration rejected: s=(%.3f,%.3f)", m.sx, m.sy);
        return false;
    }
    m.valid = true;
    s_map = m;
    Settings s("touch", true);
    s.SetInt("swap", m.swap ? 1 : 0);
    s.SetInt("ax", (int)lroundf(m.ax * 100));
    s.SetInt("ay", (int)lroundf(m.ay * 100));
    s.SetInt("sx", (int)lroundf(m.sx * 1000));
    s.SetInt("sy", (int)lroundf(m.sy * 1000));
    s.SetInt("valid", 1);
    ESP_LOGI(TAG, "Calibrated: swap=%d a=(%.1f,%.1f) s=(%.3f,%.3f)", m.swap, m.ax, m.ay, m.sx, m.sy);
    return true;
}

static volatile bool s_flip = false;
void SetFlip(bool flip) { s_flip = flip; }

void Process(esp_lcd_touch_handle_t, uint16_t* x, uint16_t* y, uint16_t*, uint8_t* point_num,
             uint8_t) {
    for (int i = 0; i < *point_num; i++) {
        float u = s_map.swap ? y[i] : x[i];
        float v = s_map.swap ? x[i] : y[i];
        float sx = kMargin + (u - s_map.ax) / s_map.sx;
        float sy = kMargin + (v - s_map.ay) / s_map.sy;
        if (!s_map.valid) {  // default guess is expressed in raw units directly
            sx = s_map.swap ? y[i] : x[i];
            sy = DISPLAY_HEIGHT - 1 - (s_map.swap ? x[i] : y[i]);
        }
        if (s_flip) {
            sx = DISPLAY_WIDTH - 1 - sx;
            sy = DISPLAY_HEIGHT - 1 - sy;
        }
        if (sx < 0) sx = 0;
        if (sy < 0) sy = 0;
        if (sx > DISPLAY_WIDTH - 1) sx = DISPLAY_WIDTH - 1;
        if (sy > DISPLAY_HEIGHT - 1) sy = DISPLAY_HEIGHT - 1;
        x[i] = (uint16_t)sx;
        y[i] = (uint16_t)sy;
    }
}

}  // namespace touch_cal
