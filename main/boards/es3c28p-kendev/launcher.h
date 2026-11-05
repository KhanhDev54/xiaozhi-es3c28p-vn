#ifndef KENDEV_LAUNCHER_H
#define KENDEV_LAUNCHER_H

// KenDev launcher for the 2.8" ES3C28P (320x240, touch):
//  - Home: 2 horizontal pages (clock + weather + lunar, month calendar)
//  - Swipe up: app menu (Chat, FM, Lịch, Thời tiết, Báo thức, Giá vàng, Hệ thống, Cài đặt)
//  - Swipe down: control panel (volume, brightness, touch calibration, restart)
// Shown on lv_layer_top while the assistant is idle; a conversation hides it and the
// home screen comes back a few seconds after the device is idle again.

#include <cstdint>
#include <functional>
#include <string>

#include <lvgl.h>

class Display;
class Backlight;
class WeatherIcon;

class Launcher {
public:
    static Launcher& GetInstance() {
        static Launcher instance;
        return instance;
    }

    void Start(Display* display, Backlight* backlight);

private:
    enum class Mode { kHidden, kHome, kMenu, kApp, kControl, kCalibrate };

    Launcher() = default;
    static void TaskEntry(void* arg);
    void Tick();
    void Build();
    void BuildHome();
    void BuildMenu();
    void BuildControl();
    void UpdateHome(bool full);
    void UpdateCalendar();

    void Show(Mode mode);
    void OpenApp(int id);
    void StartCalibration();
    void CalibrationStep();

    lv_obj_t* NewPanel();  // full-screen panel inside root_
    lv_obj_t* Label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text);
    void RunAsync(std::function<void()> fn);

    static void OnGesture(lv_event_t* e);

    Display* display_ = nullptr;
    Backlight* backlight_ = nullptr;
    const lv_font_t* text_font_ = nullptr;

    Mode mode_ = Mode::kHidden;
    int64_t busy_until_us_ = 0;   // keep hidden until then (after a conversation)
    int tick_ = 0;
    int last_minute_ = -1;
    int cal_day_ = -1;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* home_ = nullptr;     // tileview
    lv_obj_t* menu_ = nullptr;
    lv_obj_t* app_ = nullptr;
    lv_obj_t* ctrl_ = nullptr;
    lv_obj_t* calib_ = nullptr;

    // home page widgets
    lv_obj_t* date_lbl_ = nullptr;
    lv_obj_t* time_lbl_ = nullptr;
    lv_obj_t* sec_lbl_ = nullptr;
    lv_obj_t* aqi_chip_ = nullptr;
    lv_obj_t* aqi_lbl_ = nullptr;
    lv_obj_t* temp_lbl_ = nullptr;
    lv_obj_t* desc_lbl_ = nullptr;
    lv_obj_t* lunar_lbl_ = nullptr;
    lv_obj_t* city_lbl_ = nullptr;
    lv_obj_t* detail_lbl_ = nullptr;
    WeatherIcon* icon_ = nullptr;
    lv_obj_t* fc_day_[4] = {};
    lv_obj_t* fc_range_[4] = {};
    lv_obj_t* fc_rain_[4] = {};
    WeatherIcon* fc_icon_[4] = {};

    // calendar page widgets
    lv_obj_t* cal_big_ = nullptr;
    lv_obj_t* cal_info_ = nullptr;
    lv_obj_t* cal_title_ = nullptr;
    lv_obj_t* cal_cells_[42] = {};
    lv_obj_t* cal_lunar_[42] = {};

    // menu status bar
    lv_obj_t* menu_time_ = nullptr;

    // control panel
    lv_obj_t* vol_slider_ = nullptr;
    lv_obj_t* vol_lbl_ = nullptr;
    lv_obj_t* bri_slider_ = nullptr;
    lv_obj_t* bri_lbl_ = nullptr;

    // apps that refresh
    int app_id_ = -1;
    lv_obj_t* app_dynamic_ = nullptr;

    // calibration
    lv_obj_t* calib_target_ = nullptr;
    lv_obj_t* calib_text_ = nullptr;
    lv_timer_t* calib_timer_ = nullptr;
    int calib_step_ = 0;
    bool calib_was_pressed_ = false;
    uint16_t calib_raw_[2][2] = {};
    uint16_t calib_last_[2] = {};
    int calib_done_ticks_ = 0;
};

#endif  // KENDEV_LAUNCHER_H
