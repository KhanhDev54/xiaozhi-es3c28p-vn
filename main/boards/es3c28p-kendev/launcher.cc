#include "launcher.h"

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <material_symbols.h>
#include <wifi_manager.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "alarm_manager.h"
#include "application.h"
#include "backlight.h"
#include "board.h"
#include "display.h"
#include "ft6336_touch.h"
#include "info_services.h"
#include "lunar_calendar.h"
#include "radio_player.h"
#include "touch_cal.h"
#include "weather_icon.h"

#define TAG "Launcher"

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(font_material_symbols_30_4);

static const int W = 320;
static const int H = 240;
static const uint32_t kBg = 0x0B1220;
static const uint32_t kCard = 0x141C30;
static const uint32_t kText = 0xF1F5F9;
static const uint32_t kMuted = 0x94A3B8;
static const uint32_t kGold = 0xFBBF24;
static const uint32_t kAccent = 0x60A5FA;
static const int kReturnHomeSeconds = 6;

static const char* kWeekdayLong[] = {"Chủ nhật", "Thứ hai", "Thứ ba", "Thứ tư", "Thứ năm", "Thứ sáu", "Thứ bảy"};

enum AppId { kAppChat, kAppFm, kAppCalendar, kAppWeather, kAppAlarm, kAppGold, kAppSystem, kAppSettings, kAppCount };

struct AppInfo {
    const char* name;
    const char* icon;  // material symbol, nullptr = weather pictogram
    uint32_t c1, c2;   // gradient
};

static const AppInfo kApps[kAppCount] = {
    {"Chat", MATERIAL_SYMBOLS_CHAT_BUBBLE, 0x38BDF8, 0x6366F1},
    {"FM", MATERIAL_SYMBOLS_HEADPHONES, 0xE879F9, 0x9333EA},
    {"Lịch", MATERIAL_SYMBOLS_CALENDAR_MONTH, 0xF87171, 0xDC2626},
    {"Thời tiết", nullptr, 0x60A5FA, 0x2563EB},
    {"Báo thức", MATERIAL_SYMBOLS_ALARM, 0xFDBA74, 0xF97316},
    {"Giá vàng", MATERIAL_SYMBOLS_STAR, 0xFDE047, 0xCA8A04},
    {"Hệ thống", MATERIAL_SYMBOLS_MEMORY, 0x9CA3AF, 0x4B5563},
    {"Cài đặt", MATERIAL_SYMBOLS_SETTINGS, 0x94A3B8, 0x475569},
};

static std::string FormatVnd(double v) {
    char raw[32];
    snprintf(raw, sizeof(raw), "%.0f", v);
    std::string s = raw, out;
    int n = (int)s.size();
    for (int i = 0; i < n; i++) {
        out += s[i];
        int left = n - 1 - i;
        if (left > 0 && left % 3 == 0) out += '.';
    }
    return out;
}

static uint32_t AqiColor(int aqi) {
    if (aqi <= 50) return 0x22C55E;
    if (aqi <= 100) return 0xEAB308;
    if (aqi <= 150) return 0xF97316;
    if (aqi <= 200) return 0xEF4444;
    return 0xA855F7;
}

static const char* AqiText(int aqi) {
    if (aqi <= 50) return "Tốt";
    if (aqi <= 100) return "Trung bình";
    if (aqi <= 150) return "Kém";
    if (aqi <= 200) return "Xấu";
    return "Rất xấu";
}

// ---------------------------------------------------------------- lifecycle

void Launcher::Start(Display* display, Backlight* backlight) {
    display_ = display;
    backlight_ = backlight;
    touch_cal::Load();
    xTaskCreate(TaskEntry, "launcher", 6144, this, 2, nullptr);
}

void Launcher::TaskEntry(void* arg) {
    auto* self = static_cast<Launcher*>(arg);
    // Wait for SetupUI and the assets (Vietnamese font) before building
    vTaskDelay(pdMS_TO_TICKS(6000));
    while (true) {
        self->Tick();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void Launcher::RunAsync(std::function<void()> fn) {
    auto* heap_fn = new std::function<void()>(std::move(fn));
    if (xTaskCreate([](void* arg) {
            auto* f = static_cast<std::function<void()>*>(arg);
            (*f)();
            delete f;
            vTaskDelete(nullptr);
        }, "launcher_job", 8192, heap_fn, 3, nullptr) != pdPASS) {
        delete heap_fn;
    }
}

void Launcher::Tick() {
    tick_++;
    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();
    bool idle = state == kDeviceStateIdle && !AlarmManager::GetInstance().IsRinging();
    int64_t now_us = esp_timer_get_time();
    if (!idle) busy_until_us_ = now_us + (int64_t)kReturnHomeSeconds * 1000000;

    // Weather in the background (only while idle)
    if (idle) {
        bool have = InfoServices::GetInstance().GetWeather().valid;
        if ((!have && tick_ % 60 == 3) || (have && tick_ % 1800 == 0)) {
            InfoServices::GetInstance().RefreshWeather(1800);
        }
    }

    DisplayLockGuard lock(display_);
    if (root_ == nullptr) {
        Build();
        if (!touch_cal::IsCalibrated()) {
            StartCalibration();
        }
    }

    if (mode_ == Mode::kCalibrate) return;
    if (!idle) {
        if (mode_ != Mode::kHidden) Show(Mode::kHidden);
        return;
    }
    if (mode_ == Mode::kHidden && now_us >= busy_until_us_) {
        Show(Mode::kHome);
    }
    if (mode_ == Mode::kHome) UpdateHome(false);
    if (mode_ == Mode::kMenu && menu_time_) {
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        lv_label_set_text_fmt(menu_time_, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    }
    if (mode_ == Mode::kApp) {
        // Refresh the FM app when playback starts/stops, the alarm app while a timer runs
        static bool fm_shown_playing = false;
        bool playing = RadioPlayer::GetInstance().IsPlaying();
        if (app_id_ == kAppFm && playing != fm_shown_playing) {
            fm_shown_playing = playing;
            OpenApp(app_id_);
        } else if (app_id_ == kAppAlarm && AlarmManager::GetInstance().TimerRemaining() > 0) {
            OpenApp(app_id_);
        }
    }
}

// ---------------------------------------------------------------- helpers

lv_obj_t* Launcher::Label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font ? font : text_font_, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t* Launcher::NewPanel() {
    lv_obj_t* p = lv_obj_create(root_);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, W, H);
    lv_obj_set_style_bg_color(p, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(p, OnGesture, LV_EVENT_GESTURE, this);
    return p;
}

static lv_obj_t* Card(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(kCard), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}

void Launcher::OnGesture(lv_event_t* e) {
    auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    switch (self->mode_) {
        case Mode::kHome:
            if (dir == LV_DIR_TOP) self->Show(Mode::kMenu);
            else if (dir == LV_DIR_BOTTOM) self->Show(Mode::kControl);
            else return;
            break;
        case Mode::kMenu:
            if (dir == LV_DIR_BOTTOM) self->Show(Mode::kHome);
            else return;
            break;
        case Mode::kApp:
            if (dir == LV_DIR_TOP) self->Show(Mode::kMenu);
            else if (dir == LV_DIR_BOTTOM) self->Show(Mode::kHome);
            else return;
            break;
        case Mode::kControl:
            if (dir == LV_DIR_TOP) self->Show(Mode::kHome);
            else return;
            break;
        default:
            return;
    }
    lv_indev_wait_release(lv_indev_active());
}

// ---------------------------------------------------------------- build

void Launcher::Build() {
    text_font_ = lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN);
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, W, H);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    BuildHome();
    BuildMenu();
    BuildControl();
    app_ = NewPanel();
    ESP_LOGI(TAG, "UI built");
}

void Launcher::BuildHome() {
    home_ = lv_tileview_create(root_);
    lv_obj_set_size(home_, W, H);
    lv_obj_set_style_bg_color(home_, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(home_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(home_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(home_, OnGesture, LV_EVENT_GESTURE, this);
    lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);

    // ---- page 1: clock + weather
    lv_obj_t* t1 = lv_tileview_add_tile(home_, 0, 0, LV_DIR_RIGHT);
    lv_obj_add_event_cb(t1, OnGesture, LV_EVENT_GESTURE, this);

    date_lbl_ = Label(t1, nullptr, kText, "");
    lv_obj_set_pos(date_lbl_, 10, 6);

    aqi_chip_ = lv_obj_create(t1);
    lv_obj_remove_style_all(aqi_chip_);
    lv_obj_set_size(aqi_chip_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(aqi_chip_, 8, 0);
    lv_obj_set_style_pad_ver(aqi_chip_, 3, 0);
    lv_obj_set_style_radius(aqi_chip_, 10, 0);
    lv_obj_set_style_bg_opa(aqi_chip_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(aqi_chip_, LV_OBJ_FLAG_CLICKABLE);
    aqi_lbl_ = Label(aqi_chip_, &lv_font_montserrat_14, 0x0B1220, "");
    lv_obj_align(aqi_chip_, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_add_flag(aqi_chip_, LV_OBJ_FLAG_HIDDEN);

    time_lbl_ = Label(t1, &lv_font_montserrat_48, kText, "--:--");
    lv_obj_set_pos(time_lbl_, 8, 32);
    sec_lbl_ = Label(t1, &lv_font_montserrat_28, kMuted, "");
    lv_obj_align_to(sec_lbl_, time_lbl_, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);

    icon_ = new WeatherIcon(t1, 48);
    lv_obj_set_pos(icon_->obj(), 196, 36);
    temp_lbl_ = Label(t1, &lv_font_montserrat_28, kText, "");
    lv_obj_set_pos(temp_lbl_, 252, 46);
    desc_lbl_ = Label(t1, nullptr, kMuted, "");
    lv_obj_set_width(desc_lbl_, 124);
    lv_label_set_long_mode(desc_lbl_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(desc_lbl_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(desc_lbl_, 190, 88);

    lunar_lbl_ = Label(t1, nullptr, kGold, "");
    lv_obj_set_width(lunar_lbl_, 180);
    lv_label_set_long_mode(lunar_lbl_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(lunar_lbl_, 10, 90);
    city_lbl_ = Label(t1, nullptr, kMuted, "");
    lv_obj_set_width(city_lbl_, 180);
    lv_label_set_long_mode(city_lbl_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(city_lbl_, 10, 114);

    detail_lbl_ = Label(t1, nullptr, kText, "");
    lv_obj_set_width(detail_lbl_, W - 20);
    lv_label_set_long_mode(detail_lbl_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(detail_lbl_, 10, 140);

    for (int i = 0; i < 4; i++) {
        lv_obj_t* c = Card(t1, 6 + i * 78, 170, 74, 64);
        fc_day_[i] = Label(c, nullptr, kText, "");
        lv_obj_set_pos(fc_day_[i], 6, 2);
        fc_icon_[i] = new WeatherIcon(c, 22);
        lv_obj_set_pos(fc_icon_[i]->obj(), 46, 5);
        fc_range_[i] = Label(c, &lv_font_montserrat_14, kText, "");
        lv_obj_set_pos(fc_range_[i], 6, 28);
        fc_rain_[i] = Label(c, &lv_font_montserrat_14, kAccent, "");
        lv_obj_set_pos(fc_rain_[i], 6, 44);
    }

    // ---- page 2: month calendar
    lv_obj_t* t2 = lv_tileview_add_tile(home_, 1, 0, LV_DIR_LEFT);
    lv_obj_add_event_cb(t2, OnGesture, LV_EVENT_GESTURE, this);
    lv_obj_t* left = Card(t2, 6, 6, 112, 228);
    cal_big_ = Label(left, &lv_font_montserrat_48, kGold, "");
    lv_obj_set_pos(cal_big_, 8, 4);
    cal_info_ = Label(left, nullptr, kText, "");
    lv_obj_set_width(cal_info_, 100);
    lv_obj_set_height(cal_info_, 160);
    lv_label_set_long_mode(cal_info_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(cal_info_, 8, 60);

    cal_title_ = Label(t2, nullptr, kText, "");
    lv_obj_set_pos(cal_title_, 126, 4);
    static const char* kHead[] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};
    const int gx = 124, cw = 27, gy = 30;
    for (int c = 0; c < 7; c++) {
        lv_obj_t* h = Label(t2, &lv_font_montserrat_14, c == 6 ? 0xF87171 : kMuted, kHead[c]);
        lv_obj_set_width(h, cw);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(h, gx + c * cw, gy);
    }
    for (int i = 0; i < 42; i++) {
        int r = i / 7, c = i % 7;
        lv_obj_t* cell = lv_obj_create(t2);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, cw - 1, 30);
        lv_obj_set_pos(cell, gx + c * cw, gy + 18 + r * 31);
        lv_obj_set_style_radius(cell, 6, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* d = Label(cell, &lv_font_montserrat_14, kText, "");
        lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_t* l = Label(cell, &lv_font_montserrat_14, kMuted, "");
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, 2);
        cal_cells_[i] = cell;
        cal_lunar_[i] = l;
        lv_obj_set_user_data(cell, d);
    }
}

void Launcher::BuildMenu() {
    menu_ = NewPanel();
    lv_obj_t* bar_date = Label(menu_, nullptr, kText, "");
    lv_obj_set_pos(bar_date, 10, 6);
    lv_obj_set_user_data(menu_, bar_date);
    menu_time_ = Label(menu_, &lv_font_montserrat_14, kText, "");
    lv_obj_align(menu_time_, LV_ALIGN_TOP_RIGHT, -10, 10);

    for (int i = 0; i < kAppCount; i++) {
        int col = i % 4, row = i / 4;
        lv_obj_t* btn = lv_obj_create(menu_);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 72, 88);
        lv_obj_set_pos(btn, 8 + col * 77, 36 + row * 96);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_opa(btn, LV_OPA_70, LV_STATE_PRESSED);

        lv_obj_t* tile = lv_obj_create(btn);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 56, 56);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(tile, 14, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(kApps[i].c1), 0);
        lv_obj_set_style_bg_grad_color(tile, lv_color_hex(kApps[i].c2), 0);
        lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        if (kApps[i].icon) {
            lv_obj_t* ic = Label(tile, &font_material_symbols_30_4, 0xFFFFFF, kApps[i].icon);
            lv_obj_center(ic);
        } else {
            auto* w = new WeatherIcon(tile, 34);
            w->Set(2, true);
            lv_obj_center(w->obj());
        }
        lv_obj_t* name = Label(btn, nullptr, kText, kApps[i].name);
        lv_obj_set_width(name, 72);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 60);

        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
            self->OpenApp(id);
        }, LV_EVENT_CLICKED, this);
    }
    lv_obj_t* hint2 = Label(menu_, nullptr, kMuted, "Vuốt xuống để đóng");
    lv_obj_align(hint2, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void Launcher::BuildControl() {
    ctrl_ = NewPanel();
    lv_obj_t* title = Label(ctrl_, nullptr, kText, "Điều khiển");
    lv_obj_set_pos(title, 12, 8);
    lv_obj_t* hint = Label(ctrl_, nullptr, kMuted, "Vuốt lên để đóng");
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -12, 8);

    auto make_slider = [this](int y, const char* icon, lv_obj_t** slider, lv_obj_t** value) {
        lv_obj_t* ic = Label(ctrl_, &font_material_symbols_30_4, kAccent, icon);
        lv_obj_set_pos(ic, 12, y - 6);
        lv_obj_t* s = lv_slider_create(ctrl_);
        lv_obj_set_size(s, 190, 10);
        lv_obj_set_pos(s, 56, y + 8);
        lv_slider_set_range(s, 0, 100);
        lv_obj_set_style_bg_color(s, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s, lv_color_hex(kAccent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t* v = Label(ctrl_, &lv_font_montserrat_14, kText, "");
        lv_obj_set_pos(v, 262, y + 4);
        *slider = s;
        *value = v;
    };
    make_slider(48, MATERIAL_SYMBOLS_VOLUME_UP, &vol_slider_, &vol_lbl_);
    make_slider(98, MATERIAL_SYMBOLS_BRIGHTNESS_6, &bri_slider_, &bri_lbl_);

    lv_obj_add_event_cb(vol_slider_, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        int v = lv_slider_get_value(self->vol_slider_);
        lv_label_set_text_fmt(self->vol_lbl_, "%d%%", v);
        if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
            Board::GetInstance().GetAudioCodec()->SetOutputVolume(v);
        }
    }, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(bri_slider_, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        int v = lv_slider_get_value(self->bri_slider_);
        if (v < 5) v = 5;
        lv_label_set_text_fmt(self->bri_lbl_, "%d%%", v);
        if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
            self->backlight_->SetBrightness((uint8_t)v, false);
        } else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
            self->backlight_->SetBrightness((uint8_t)v, true);  // remember
        }
    }, LV_EVENT_ALL, this);

    auto make_button = [this](int x, int y, int w, const char* text, uint32_t color, lv_event_cb_t cb) {
        lv_obj_t* b = lv_button_create(ctrl_);
        lv_obj_set_size(b, w, 44);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_t* l = Label(b, nullptr, color, text);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, this);
    };
    make_button(10, 150, 96, "Chat", kText, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->OpenApp(kAppChat);
    });
    make_button(112, 150, 96, "Cảm ứng", kText, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->StartCalibration();
    });
    make_button(214, 150, 96, "Khởi động", 0xF87171, [](lv_event_t*) { esp_restart(); });
}

// ---------------------------------------------------------------- navigation

void Launcher::Show(Mode mode) {
    if (root_ == nullptr) return;
    lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(app_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctrl_, LV_OBJ_FLAG_HIDDEN);
    if (mode != Mode::kApp) app_id_ = -1;
    mode_ = mode;

    if (mode == Mode::kHidden) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);

    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    switch (mode) {
        case Mode::kHome:
            lv_obj_remove_flag(home_, LV_OBJ_FLAG_HIDDEN);
            UpdateHome(true);
            break;
        case Mode::kMenu: {
            lv_obj_t* d = static_cast<lv_obj_t*>(lv_obj_get_user_data(menu_));
            lv_label_set_text_fmt(d, "%s, %02d/%02d", kWeekdayLong[tmv.tm_wday % 7], tmv.tm_mday, tmv.tm_mon + 1);
            lv_label_set_text_fmt(menu_time_, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            lv_obj_remove_flag(menu_, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        case Mode::kApp:
            lv_obj_remove_flag(app_, LV_OBJ_FLAG_HIDDEN);
            break;
        case Mode::kControl: {
            int vol = Board::GetInstance().GetAudioCodec()->output_volume();
            int bri = backlight_ ? backlight_->brightness() : 100;
            lv_slider_set_value(vol_slider_, vol, LV_ANIM_OFF);
            lv_slider_set_value(bri_slider_, bri, LV_ANIM_OFF);
            lv_label_set_text_fmt(vol_lbl_, "%d%%", vol);
            lv_label_set_text_fmt(bri_lbl_, "%d%%", bri);
            lv_obj_remove_flag(ctrl_, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------- home pages

void Launcher::UpdateHome(bool full) {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    bool time_ok = tmv.tm_year + 1900 >= 2024;
    if (!time_ok) {
        lv_label_set_text(time_lbl_, "--:--");
        lv_label_set_text(sec_lbl_, "");
        lv_label_set_text(date_lbl_, "Đang đồng bộ giờ…");
        return;
    }
    lv_label_set_text_fmt(time_lbl_, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    lv_label_set_text_fmt(sec_lbl_, "%02d", tmv.tm_sec);
    lv_obj_align_to(sec_lbl_, time_lbl_, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);

    if (!full && tmv.tm_min == last_minute_ && tick_ % 30 != 0) return;
    last_minute_ = tmv.tm_min;

    lv_label_set_text_fmt(date_lbl_, "%s, %02d/%02d/%04d", kWeekdayLong[tmv.tm_wday % 7], tmv.tm_mday,
                          tmv.tm_mon + 1, tmv.tm_year + 1900);
    auto ld = lunar::SolarToLunar(tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
    std::string lunar = lunar::ShortText(ld) + " · " + lunar::YearCanChi(ld.year);
    lv_label_set_text(lunar_lbl_, lunar.c_str());

    WeatherNow w = InfoServices::GetInstance().GetWeather();
    if (w.valid) {
        icon_->Set(w.code, w.is_day);
        lv_label_set_text_fmt(temp_lbl_, "%d°", w.temp);
        lv_label_set_text(desc_lbl_, w.desc.c_str());
        lv_label_set_text(city_lbl_, w.city.c_str());
        char detail[128];
        snprintf(detail, sizeof(detail), "Cảm giác %d°  ·  Ẩm %d%%  ·  Gió %.0f km/h", w.feels_like, w.humidity,
                 w.wind * 3.6f);
        lv_label_set_text(detail_lbl_, detail);
        if (w.aqi >= 0) {
            lv_label_set_text_fmt(aqi_lbl_, "AQI %d", w.aqi);
            lv_obj_set_style_bg_color(aqi_chip_, lv_color_hex(AqiColor(w.aqi)), 0);
            lv_obj_remove_flag(aqi_chip_, LV_OBJ_FLAG_HIDDEN);
        }
        for (int i = 0; i < 4; i++) {
            if (i < (int)w.days.size()) {
                const auto& d = w.days[i];
                lv_label_set_text(fc_day_[i], i == 0 ? "Nay" : d.day.c_str());
                fc_icon_[i]->Set(d.code, true);
                lv_label_set_text_fmt(fc_range_[i], "%d-%d°", d.tmin, d.tmax);
                lv_label_set_text_fmt(fc_rain_[i], "%d%%", d.rain_prob);
            }
        }
    } else {
        lv_label_set_text(desc_lbl_, "Đang tải thời tiết…");
    }
    if (cal_day_ != tmv.tm_mday) UpdateCalendar();
}

void Launcher::UpdateCalendar() {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    cal_day_ = tmv.tm_mday;
    int y = tmv.tm_year + 1900, m = tmv.tm_mon + 1;
    auto today = lunar::SolarToLunar(tmv.tm_mday, m, y);

    lv_label_set_text_fmt(cal_big_, "%d", tmv.tm_mday);
    std::string info = std::string("Tháng ") + std::to_string(today.month) + (today.leap ? " nhuận" : "") +
                       " ÂL\nNgày " + std::to_string(today.day) + "\nNăm " + lunar::YearCanChi(today.year) +
                       "\nNgày " + lunar::DayCanChi(tmv.tm_mday, m, y) + "\nGiờ tốt: " +
                       lunar::AuspiciousHours(tmv.tm_mday, m, y);
    lv_label_set_text(cal_info_, info.c_str());
    lv_label_set_text_fmt(cal_title_, "Tháng %d · %d", m, y);

    struct tm first = {};
    first.tm_year = y - 1900;
    first.tm_mon = m - 1;
    first.tm_mday = 1;
    first.tm_hour = 12;
    mktime(&first);
    int lead = (first.tm_wday + 6) % 7;  // Monday first
    for (int i = 0; i < 42; i++) {
        struct tm d = first;
        d.tm_mday = 1 - lead + i;
        mktime(&d);
        auto ld = lunar::SolarToLunar(d.tm_mday, d.tm_mon + 1, d.tm_year + 1900);
        lv_obj_t* cell = cal_cells_[i];
        lv_obj_t* num = static_cast<lv_obj_t*>(lv_obj_get_user_data(cell));
        bool in_month = d.tm_mon == m - 1;
        bool is_today = in_month && d.tm_mday == tmv.tm_mday;
        lv_label_set_text_fmt(num, "%d", d.tm_mday);
        uint32_t color = !in_month ? 0x475569 : (d.tm_wday == 0 ? 0xF87171 : kText);
        lv_obj_set_style_text_color(num, lv_color_hex(is_today ? 0xFFFFFF : color), 0);
        if (ld.day == 1) {
            lv_label_set_text_fmt(cal_lunar_[i], "%d/%d", ld.day, ld.month);
            lv_obj_set_style_text_color(cal_lunar_[i], lv_color_hex(kGold), 0);
        } else {
            lv_label_set_text_fmt(cal_lunar_[i], "%d", ld.day);
            lv_obj_set_style_text_color(cal_lunar_[i], lv_color_hex(ld.day == 15 ? kGold : kMuted), 0);
        }
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x2563EB), 0);
        lv_obj_set_style_bg_opa(cell, is_today ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (i >= 35 && !in_month) {
            lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---------------------------------------------------------------- apps

void Launcher::OpenApp(int id) {
    if (id == kAppChat) {
        Show(Mode::kHidden);
        busy_until_us_ = esp_timer_get_time() + 4000000;
        Application::GetInstance().Schedule([]() { Application::GetInstance().ToggleChatState(); });
        return;
    }
    if (id == kAppCalendar) {
        Show(Mode::kHome);
        lv_obj_set_tile_id(home_, 1, 0, LV_ANIM_ON);
        return;
    }

    bool refresh = (mode_ == Mode::kApp && app_id_ == id);
    lv_obj_clean(app_);
    app_id_ = id;
    lv_obj_t* title = Label(app_, nullptr, kText, kApps[id].name);
    lv_obj_set_pos(title, 12, 8);
    lv_obj_t* hint = Label(app_, nullptr, kMuted, "Vuốt lên: menu");
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -12, 8);

    switch (id) {
        case kAppFm: {
            auto& radio = RadioPlayer::GetInstance();
            lv_obj_t* now = Card(app_, 8, 38, W - 16, 52);
            lv_obj_t* name = Label(now, nullptr, radio.IsPlaying() ? kText : kMuted,
                                   radio.IsPlaying() ? radio.CurrentName().c_str() : "Chưa phát");
            lv_obj_set_width(name, 210);
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
            if (radio.IsPlaying()) {
                lv_obj_t* stop = lv_button_create(now);
                lv_obj_set_size(stop, 70, 36);
                lv_obj_align(stop, LV_ALIGN_RIGHT_MID, -8, 0);
                lv_obj_set_style_bg_color(stop, lv_color_hex(0xDC2626), 0);
                lv_obj_set_style_radius(stop, 18, 0);
                lv_obj_t* l = Label(stop, nullptr, 0xFFFFFF, "Dừng");
                lv_obj_center(l);
                lv_obj_add_event_cb(stop, [](lv_event_t* e) {
                    auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
                    self->RunAsync([]() { RadioPlayer::GetInstance().Stop(); });
                }, LV_EVENT_CLICKED, this);
            }
            int n = RadioPlayer::StationCount();
            for (int i = 0; i < n && i < 9; i++) {
                int col = i % 3, row = i / 3;
                lv_obj_t* b = lv_button_create(app_);
                lv_obj_set_size(b, 98, 40);
                lv_obj_set_pos(b, 8 + col * 104, 98 + row * 46);
                bool active = radio.IsPlaying() && radio.CurrentName() == RadioPlayer::StationName(i);
                lv_obj_set_style_bg_color(b, lv_color_hex(active ? 0x7C3AED : 0x1E293B), 0);
                lv_obj_set_style_radius(b, 10, 0);
                lv_obj_set_style_shadow_width(b, 0, 0);
                lv_obj_t* l = Label(b, &lv_font_montserrat_14, kText, RadioPlayer::StationKey(i));
                lv_obj_center(l);
                lv_obj_set_user_data(b, (void*)(intptr_t)i);
                lv_obj_add_event_cb(b, [](lv_event_t* e) {
                    auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
                    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
                    std::string key = RadioPlayer::StationKey(idx);
                    self->RunAsync([key]() { RadioPlayer::GetInstance().Play(key); });
                }, LV_EVENT_CLICKED, this);
            }
            break;
        }
        case kAppWeather: {
            WeatherNow w = InfoServices::GetInstance().GetWeather();
            lv_obj_t* body = Label(app_, nullptr, kText, "");
            lv_obj_set_width(body, W - 24);
            lv_obj_set_pos(body, 12, 40);
            if (!w.valid) {
                lv_label_set_text(body, "Chưa có dữ liệu thời tiết. Kiểm tra mạng.");
                break;
            }
            std::string s = w.city + "\n" + std::to_string(w.temp) + "° · " + w.desc + "\nCảm giác " +
                            std::to_string(w.feels_like) + "° · Ẩm " + std::to_string(w.humidity) + "%";
            if (w.aqi >= 0) s += "\nAQI " + std::to_string(w.aqi) + " · " + AqiText(w.aqi);
            for (size_t i = 0; i < w.days.size() && i < 4; i++) {
                const auto& d = w.days[i];
                s += "\n" + std::string(i == 0 ? "Hôm nay" : d.day) + ": " + d.desc + ", " + std::to_string(d.tmin) +
                     "-" + std::to_string(d.tmax) + "°, mưa " + std::to_string(d.rain_prob) + "%";
            }
            lv_label_set_text(body, s.c_str());
            break;
        }
        case kAppAlarm: {
            auto& am = AlarmManager::GetInstance();
            auto alarms = am.ListAlarms();
            std::string s;
            int remain = am.TimerRemaining();
            if (remain > 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Hẹn giờ: còn %02d:%02d\n", remain / 60, remain % 60);
                s += buf;
            }
            if (alarms.empty()) {
                s += "Chưa có báo thức nào.";
            }
            for (const auto& a : alarms) {
                char buf[96];
                snprintf(buf, sizeof(buf), "%02d:%02d  %s%s%s\n", a.hour, a.minute,
                         a.repeat_daily ? "Hằng ngày" : "Một lần", a.label.empty() ? "" : " · ", a.label.c_str());
                s += buf;
            }
            s += "\nNói: \"đặt báo thức 6 giờ 30\" hoặc \"hẹn giờ 10 phút\".";
            lv_obj_t* body = Label(app_, nullptr, kText, s.c_str());
            lv_obj_set_width(body, W - 24);
            lv_obj_set_pos(body, 12, 40);
            if (am.IsRinging()) {
                lv_obj_t* b = lv_button_create(app_);
                lv_obj_set_size(b, 120, 40);
                lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -10);
                lv_obj_t* l = Label(b, nullptr, 0xFFFFFF, "Tắt chuông");
                lv_obj_center(l);
                lv_obj_add_event_cb(b, [](lv_event_t*) { AlarmManager::GetInstance().StopRinging(); },
                                    LV_EVENT_CLICKED, nullptr);
            }
            break;
        }
        case kAppGold: {
            lv_obj_t* body = Label(app_, nullptr, kMuted, "Đang tải giá vàng SJC…");
            lv_obj_set_width(body, W - 24);
            lv_obj_set_pos(body, 12, 40);
            app_dynamic_ = body;
            if (refresh) break;
            RunAsync([this, body]() {
                std::vector<GoldRow> rows;
                std::string date;
                bool ok = InfoServices::GetInstance().FetchGold("Hồ Chí Minh", rows, date);
                std::string s;
                if (!ok) {
                    s = "Không lấy được giá vàng từ SJC. Thử lại sau.";
                } else {
                    s = "SJC · " + date + " · triệu đồng/lượng\n";
                    for (size_t i = 0; i < rows.size() && i < 5; i++) {
                        char buf[160];
                        snprintf(buf, sizeof(buf), "%s\n   Mua %.2f · Bán %.2f\n", rows[i].type.c_str(),
                                 rows[i].buy / 1e6, rows[i].sell / 1e6);
                        s += buf;
                    }
                }
                DisplayLockGuard lock(display_);
                if (mode_ == Mode::kApp && app_id_ == kAppGold && app_dynamic_ == body) {
                    lv_label_set_text(body, s.c_str());
                    lv_obj_set_style_text_color(body, lv_color_hex(kText), 0);
                }
            });
            break;
        }
        case kAppSystem: {
            auto& wifi = WifiManager::GetInstance();
            int64_t up = esp_timer_get_time() / 1000000;
            size_t sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            float temp = 0;
            bool has_temp = Board::GetInstance().GetTemperature(temp);
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "Wi-Fi: %s (%d dBm)\nIP: %s\nRAM trống: %u KB · PSRAM: %.1f MB\n"
                     "Chạy: %d giờ %d phút\nFirmware: KenDev %s%s%s",
                     wifi.GetSsid().c_str(), wifi.GetRssi(), wifi.GetIpAddress().c_str(), (unsigned)(sram / 1024),
                     psram / 1048576.0, (int)(up / 3600), (int)(up % 3600 / 60), esp_app_get_description()->version,
                     has_temp ? "\nNhiệt độ chip: " : "", has_temp ? (std::to_string((int)temp) + "°C").c_str() : "");
            lv_obj_t* body = Label(app_, nullptr, kText, buf);
            lv_obj_set_width(body, W - 24);
            lv_obj_set_pos(body, 12, 40);
            break;
        }
        case kAppSettings: {
            lv_obj_t* body = Label(app_, nullptr, kText,
                                   "Đổi Wi-Fi / thành phố thời tiết: bấm nút BOOT lúc máy đang khởi động, "
                                   "rồi mở 192.168.4.1.\nVuốt xuống ở màn hình chính để chỉnh âm lượng, độ sáng.");
            lv_obj_set_width(body, W - 24);
            lv_obj_set_pos(body, 12, 40);
            lv_obj_t* b = lv_button_create(app_);
            lv_obj_set_size(b, 180, 42);
            lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -12);
            lv_obj_set_style_bg_color(b, lv_color_hex(0x1E293B), 0);
            lv_obj_t* l = Label(b, nullptr, kText, "Cân chỉnh cảm ứng");
            lv_obj_center(l);
            lv_obj_add_event_cb(b, [](lv_event_t* e) {
                static_cast<Launcher*>(lv_event_get_user_data(e))->StartCalibration();
            }, LV_EVENT_CLICKED, this);
            break;
        }
        default:
            break;
    }
    if (mode_ != Mode::kApp) Show(Mode::kApp);
}

// ---------------------------------------------------------------- touch calibration

void Launcher::StartCalibration() {
    Show(Mode::kHidden);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    mode_ = Mode::kCalibrate;
    if (calib_ == nullptr) {
        calib_ = NewPanel();
        calib_text_ = Label(calib_, nullptr, kText, "");
        lv_obj_set_width(calib_text_, 240);
        lv_obj_set_style_text_align(calib_text_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(calib_text_, LV_ALIGN_CENTER, 0, 0);
        calib_target_ = lv_obj_create(calib_);
        lv_obj_remove_style_all(calib_target_);
        lv_obj_set_size(calib_target_, 24, 24);
        lv_obj_set_style_radius(calib_target_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(calib_target_, 3, 0);
        lv_obj_set_style_border_color(calib_target_, lv_color_hex(kGold), 0);
        lv_obj_clear_flag(calib_target_, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_remove_flag(calib_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(calib_);
    calib_step_ = 0;
    calib_was_pressed_ = false;
    calib_done_ticks_ = 0;
    lv_label_set_text(calib_text_, "Cân chỉnh cảm ứng\nChạm vào vòng tròn vàng");
    lv_obj_set_pos(calib_target_, 30 - 12, 30 - 12);
    lv_obj_remove_flag(calib_target_, LV_OBJ_FLAG_HIDDEN);
    if (calib_timer_ == nullptr) {
        calib_timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<Launcher*>(lv_timer_get_user_data(t))->CalibrationStep();
        }, 30, this);
    }
}

void Launcher::CalibrationStep() {
    if (calib_done_ticks_ > 0) {
        if (--calib_done_ticks_ == 0) {
            lv_timer_delete(calib_timer_);
            calib_timer_ = nullptr;
            lv_obj_add_flag(calib_, LV_OBJ_FLAG_HIDDEN);
            Show(Mode::kHome);
        }
        return;
    }
    uint16_t x, y;
    bool pressed = ft6336_last_raw(&x, &y);
    if (pressed) {
        calib_last_[0] = x;
        calib_last_[1] = y;
        calib_was_pressed_ = true;
        return;
    }
    if (!calib_was_pressed_) return;
    calib_was_pressed_ = false;  // released: take the last point
    calib_raw_[calib_step_][0] = calib_last_[0];
    calib_raw_[calib_step_][1] = calib_last_[1];
    ESP_LOGI(TAG, "Calibration point %d: raw (%u, %u)", calib_step_, calib_last_[0], calib_last_[1]);
    if (calib_step_ == 0) {
        calib_step_ = 1;
        lv_obj_set_pos(calib_target_, W - 30 - 12, H - 30 - 12);
        return;
    }
    bool ok = touch_cal::Solve(calib_raw_[0][0], calib_raw_[0][1], calib_raw_[1][0], calib_raw_[1][1], W, H);
    lv_obj_add_flag(calib_target_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(calib_text_, ok ? "Xong! Cảm ứng đã được cân chỉnh." : "Chưa đúng, thử lại nhé.");
    if (ok) {
        calib_done_ticks_ = 50;  // ~1.5 s
    } else {
        calib_step_ = 0;
        lv_obj_set_pos(calib_target_, 30 - 12, 30 - 12);
        lv_obj_remove_flag(calib_target_, LV_OBJ_FLAG_HIDDEN);
    }
}
