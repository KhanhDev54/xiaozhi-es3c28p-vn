#ifndef ZERO_IDLE_SCREEN_H
#define ZERO_IDLE_SCREEN_H

// Full-screen idle overlay for the 128x64 OLED: big clock, a small weather icon
// drawn with LVGL shapes, and a rotating line (weekday/date/lunar, weather/AQI,
// days until Rằm / mùng 1, radio station). Dims the OLED at night.

#include <cstdint>
#include <string>

#include <esp_lcd_panel_io.h>
#include <lvgl.h>

class Display;

class IdleScreen {
public:
    static IdleScreen& GetInstance() {
        static IdleScreen instance;
        return instance;
    }

    // Start the 1 Hz update task. Safe to call from a board constructor.
    // panel_io (optional) is used to lower the OLED contrast at night.
    void Start(Display* display, esp_lcd_panel_io_handle_t panel_io = nullptr);

    // Hide the overlay for a few seconds (e.g. a button was pressed and a
    // notification such as "Âm lượng 70" should be visible).
    void Bump(int seconds = 4);

private:
    IdleScreen() = default;
    static void TaskEntry(void* arg);
    void Tick();
    void Create();
    void SetVisible(bool visible);
    void DrawIcon(int wmo_code, bool is_day);
    void UpdateContrast(int hour);
    static std::string LunarCountdown(int day, int month, int year);

    Display* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* line_label_ = nullptr;
    lv_obj_t* icon_ = nullptr;
    lv_obj_t* dots_[3] = {};   // circles
    lv_obj_t* block_ = nullptr;  // rectangle (cloud base)
    lv_obj_t* lines_[4] = {};
    lv_point_precise_t line_pts_[4][3] = {};
    int icon_kind_ = -2;
    int contrast_ = -1;
    bool night_on_ = true;  // set on the Wi-Fi setup page ("Tùy chỉnh")
    int night_start_ = 22;
    int night_end_ = 6;
    bool visible_ = false;
    int idle_seconds_ = 0;
    int tick_ = 0;
    int64_t bump_until_us_ = 0;
    std::string countdown_cache_;
    int countdown_day_ = -1;
};

#endif  // ZERO_IDLE_SCREEN_H
