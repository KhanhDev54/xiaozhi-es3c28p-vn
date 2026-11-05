#ifndef TOUCH_CAL_H
#define TOUCH_CAL_H

// Touch -> screen coordinate mapping for the FT6336, measured on the device with a
// two-point calibration (TouchCal screen) and stored in NVS ("touch" namespace).

#include <cstdint>
#include <esp_lcd_touch.h>

namespace touch_cal {

struct Mapping {
    bool valid = false;
    bool swap = true;       // raw y -> screen x
    float ax = 0, ay = 0;   // raw value at screen (30, 30) for the u / v axis
    float sx = 1, sy = 1;   // raw units per screen pixel (sign = mirroring)
};

void Load();
bool IsCalibrated();
// Compute a mapping from two taps: screen points p1=(30,30), p2=(W-30,H-30)
bool Solve(uint16_t r1x, uint16_t r1y, uint16_t r2x, uint16_t r2y, int width, int height);
void Reset();
// Screen rotated by 180° (Xoay 180°): mirror both axes after mapping
void SetFlip(bool flip);

// esp_lcd_touch process_coordinates callback
void Process(esp_lcd_touch_handle_t tp, uint16_t* x, uint16_t* y, uint16_t* strength,
             uint8_t* point_num, uint8_t max_point_num);

}  // namespace touch_cal

#endif
