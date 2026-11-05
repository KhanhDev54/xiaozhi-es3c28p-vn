#ifndef WEATHER_ICON_H
#define WEATHER_ICON_H

// Small weather pictogram drawn with LVGL shapes (no image assets):
// sun, moon, partly cloudy, cloud, rain, storm, fog. Size in pixels (square).

#include <lvgl.h>

class WeatherIcon {
public:
    WeatherIcon(lv_obj_t* parent, int size);
    lv_obj_t* obj() const { return box_; }
    void Set(int wmo_code, bool is_day);

private:
    int S(float v) const { return (int)(v * size_ / 24.0f + 0.5f); }
    lv_obj_t* box_;
    lv_obj_t* dots_[3];
    lv_obj_t* block_;
    lv_obj_t* lines_[4];
    lv_point_precise_t pts_[4][3];
    int size_;
    int kind_ = -2;
};

#endif
