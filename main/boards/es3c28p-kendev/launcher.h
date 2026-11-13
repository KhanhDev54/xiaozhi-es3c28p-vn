#ifndef KENDEV_LAUNCHER_H
#define KENDEV_LAUNCHER_H

// KenDev launcher for the 2.8" ES3C28P (320x240, touch):
//  - Home: 3 horizontal pages (clock + weather, month calendar, Biến động số dư)
//  - Swipe up: iOS-style menu, 11 apps on 2 pages (4x2 grid)
//  - Swipe down (home or any app): control panel
//  - Overlays: new transaction, alarm ringing, night screen, "now playing" / countdown pill
// Shown on lv_layer_top while the assistant is idle; a conversation hides it and the
// home screen comes back a few seconds after the device is idle again.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <lvgl.h>

#include "bank_store.h"

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
    // Board hooks: rotate the panel by 180°, set the idle LED colour (0 = off)
    void SetHandlers(std::function<void(bool)> rotate, std::function<void(int)> led) {
        rotate_ = std::move(rotate);
        led_ = std::move(led);
    }

    // LED palette shared with the board (index 0 = off)
    static int LedColorCount();
    static uint32_t LedColor(int index);

private:
    enum class Mode { kHidden, kHome, kMenu, kApp, kControl, kCalibrate, kOverlay };
    enum class Overlay { kNone, kTx, kTxDetail, kReceived, kRing, kNight, kKeypad };

    Launcher() = default;
    static void TaskEntry(void* arg);
    void Tick();
    void Build();
    void BuildHome();
    void BuildMenu();
    void BuildControl();
    void BuildPill();
    void UpdateHome(bool full);
    void UpdateCalendar();
    void UpdateBankPage();
    void UpdatePill();
    void UpdateMenuDots();
    void UpdateHomeDots();

    void Show(Mode mode);
    void OpenApp(int id);
    void RefreshApp();          // per-second refresh of the open app
    void StartCalibration();
    void CalibrationStep();

    // overlays
    void ShowOverlay(Overlay kind);
    lv_obj_t* OverlayRoot();
    void CloseOverlay();
    void BuildTxOverlay(const BankTx& tx, bool detail);
    void BuildReceivedOverlay(const BankTx& tx);
    void BuildRingOverlay();
    void BuildNightOverlay();
    void BuildKeypadOverlay();
    void HandleNewTx();

    // apps
    void AppComingSoon(const char* text);
    void AppFm();
    void AppMusic();
    void AppTimer();
    void AppQr();
    void AppHistory();
    void AppSystem();
    void AppSettings();
    void AppWifi();
    void AppHud();             // Waze HUD: setup help, or the live navigation view
    void HudRefresh();         // 4 times a second while the HUD is open
    void HudDrawArrow(int trn, int exit);
    static void HudTimerCb(lv_timer_t* t);

    // widgets
    lv_obj_t* NewPanel();  // full-screen panel inside root_
    lv_obj_t* Label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text);
    lv_obj_t* StatusBar(lv_obj_t* parent, const char* left_text, lv_obj_t** time_label);
    lv_obj_t* Chip(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg, const lv_font_t* font = nullptr);
    lv_obj_t* Button(lv_obj_t* parent, int x, int y, int w, int h, const char* text, uint32_t bg, uint32_t fg,
                     lv_event_cb_t cb, void* user = nullptr);
    void RunAsync(std::function<void()> fn);
    bool AutoHomeAllowed() const;

    static void OnGesture(lv_event_t* e);

    Display* display_ = nullptr;
    Backlight* backlight_ = nullptr;
    const lv_font_t* text_font_ = nullptr;
    const lv_font_t* f14_ = nullptr;   // Montserrat 14 + Vietnamese fallback
    const lv_font_t* f28_ = nullptr;   // Montserrat 28 + Vietnamese fallback
    std::function<void(bool)> rotate_;
    std::function<void(int)> led_;

    Mode mode_ = Mode::kHidden;
    Mode overlay_return_ = Mode::kHome;
    Overlay overlay_ = Overlay::kNone;
    int64_t overlay_until_us_ = 0;     // auto close (0 = never)
    int64_t busy_until_us_ = 0;        // keep hidden until then (after a conversation)
    int64_t night_wake_until_us_ = 0;
    int tick_ = 0;
    int last_minute_ = -1;
    int cal_day_ = -1;
    int bank_rev_shown_ = -1;
    int tx_rev_seen_ = 0;
    std::atomic<bool> tx_pending_{false};

    lv_obj_t* root_ = nullptr;
    lv_obj_t* home_ = nullptr;     // tileview
    lv_obj_t* home_dots_[3] = {};
    lv_obj_t* menu_ = nullptr;
    lv_obj_t* menu_tiles_ = nullptr;
    lv_obj_t* menu_dots_[2] = {};
    lv_obj_t* menu_date_ = nullptr;
    lv_obj_t* menu_time_ = nullptr;
    lv_obj_t* app_panel_ = nullptr;
    lv_obj_t* app_ = nullptr;      // content of the open app (rebuilt by OpenApp)
    lv_obj_t* app_time_ = nullptr;
    lv_obj_t* ctrl_ = nullptr;
    lv_obj_t* calib_ = nullptr;
    lv_obj_t* overlay_panel_ = nullptr;
    lv_obj_t* overlay_root_ = nullptr;
    lv_obj_t* pill_ = nullptr;
    lv_obj_t* pill_lbl_ = nullptr;

    // home page 1
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

    // home page 2 (calendar)
    lv_obj_t* cal_big_ = nullptr;
    lv_obj_t* cal_info_ = nullptr;
    lv_obj_t* cal_title_ = nullptr;
    lv_obj_t* cal_cells_[42] = {};
    lv_obj_t* cal_lunar_[42] = {};

    // home page 3 (bank)
    lv_obj_t* bank_page_ = nullptr;
    lv_obj_t* bank_body_ = nullptr;
    lv_obj_t* bank_read_ = nullptr;

    // control panel
    lv_obj_t* vol_slider_ = nullptr;
    lv_obj_t* vol_lbl_ = nullptr;
    lv_obj_t* bri_slider_ = nullptr;
    lv_obj_t* bri_lbl_ = nullptr;
    lv_obj_t* led_dots_[8] = {};
    lv_obj_t* tog_home_ = nullptr;
    lv_obj_t* tog_read_ = nullptr;
    void UpdateControl();

    // apps
    int app_id_ = -1;
    lv_obj_t* app_dynamic_ = nullptr;
    int app_page_ = 0;             // QR account index / history tab
    std::vector<BankTx> list_cache_;

    // QR app
    uint64_t qr_amount_ = 0;
    uint64_t keypad_value_ = 0;
    lv_obj_t* keypad_lbl_ = nullptr;

    // Waze HUD
    lv_timer_t* hud_timer_ = nullptr;
    bool hud_live_ = false;            // live view built (else the setup help)
    bool hud_was_connected_ = false;
    int hud_rev_ = -1;
    int hud_arrow_key_ = -1;
    int hud_dst_start_ = 0;
    bool hud_stale_ = false;
    lv_obj_t* hud_body_ = nullptr;
    lv_obj_t* hud_clock_ = nullptr;
    lv_obj_t* hud_top_ = nullptr;
    lv_obj_t* hud_lanes_ = nullptr;
    lv_obj_t* hud_arrow_ = nullptr;
    lv_obj_t* hud_dist_ = nullptr;
    lv_obj_t* hud_dist_unit_ = nullptr;
    lv_obj_t* hud_dist_bar_ = nullptr;
    lv_obj_t* hud_arc_ = nullptr;
    lv_obj_t* hud_speed_ = nullptr;
    lv_obj_t* hud_limit_ = nullptr;
    lv_obj_t* hud_limit_lbl_ = nullptr;
    lv_obj_t* hud_chip_ = nullptr;
    lv_obj_t* hud_chip_lbl_ = nullptr;
    lv_obj_t* hud_alert_ = nullptr;
    lv_obj_t* hud_alert_dist_ = nullptr;
    lv_obj_t* hud_more_ = nullptr;
    lv_obj_t* hud_street_ = nullptr;
    lv_point_precise_t hud_pts_[4][10] = {};  // lv_line keeps a pointer to its points

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
