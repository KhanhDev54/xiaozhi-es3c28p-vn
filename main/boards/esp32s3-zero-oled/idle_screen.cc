#include "idle_screen.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <initializer_list>
#include <ctime>
#include <string>
#include <vector>

#include "alarm_manager.h"
#include "application.h"
#include "display.h"
#include "info_services.h"
#include "lunar_calendar.h"
#include "radio_player.h"
#include "settings.h"

#define TAG "IdleScreen"

LV_FONT_DECLARE(lv_font_montserrat_32);

static const int kShowAfterIdleSeconds = 5;
static const int kLineRotateSeconds = 5;
static const int kIconX = 102;
static const int kIconY = 6;

enum IconKind { kIconNone = -1, kIconSun, kIconMoon, kIconPartly, kIconCloud, kIconRain, kIconStorm, kIconFog };

static int IconFor(int code, bool is_day) {
    if (code < 0) return kIconNone;
    if (code <= 1) return is_day ? kIconSun : kIconMoon;
    if (code == 2) return kIconPartly;
    if (code == 3) return kIconCloud;
    if (code == 45 || code == 48) return kIconFog;
    if (code >= 95) return kIconStorm;
    return kIconRain;  // drizzle, rain, showers, snow
}

void IdleScreen::Start(Display* display, esp_lcd_panel_io_handle_t panel_io) {
    display_ = display;
    panel_io_ = panel_io;
    Settings s("idle", false);
    night_on_ = s.GetInt("night_on", 1) != 0;
    night_start_ = s.GetInt("night_start", 22);
    night_end_ = s.GetInt("night_end", 6);
    xTaskCreate(TaskEntry, "idle_screen", 6144, this, 2, nullptr);
}

void IdleScreen::Bump(int seconds) {
    bump_until_us_ = esp_timer_get_time() + (int64_t)seconds * 1000000;
}

void IdleScreen::TaskEntry(void* arg) {
    auto* self = static_cast<IdleScreen*>(arg);
    // Let the application finish booting (SetupUI, assets/fonts) before drawing
    vTaskDelay(pdMS_TO_TICKS(8000));
    while (true) {
        self->Tick();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static lv_obj_t* MakeLine(lv_obj_t* parent) {
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_set_style_line_color(l, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

static lv_obj_t* MakeShape(lv_obj_t* parent, bool circle) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    if (circle) lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

void IdleScreen::Create() {
    auto* parent = lv_layer_top();
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(c, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* t = lv_label_create(c);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_label_set_text(t, "--:--");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* l = lv_label_create(c);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_width(l, LV_HOR_RES);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -4);

    // Weather icon area (24x24), shapes are configured in DrawIcon()
    icon_ = lv_obj_create(c);
    lv_obj_remove_style_all(icon_);
    lv_obj_set_size(icon_, 24, 24);
    lv_obj_set_pos(icon_, kIconX, kIconY);
    lv_obj_clear_flag(icon_, LV_OBJ_FLAG_SCROLLABLE);
    for (auto& d : dots_) d = MakeShape(icon_, true);
    block_ = MakeShape(icon_, false);
    for (auto& ln : lines_) ln = MakeLine(icon_);
    lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);

    container_ = c;
    time_label_ = t;
    line_label_ = l;
}

void IdleScreen::DrawIcon(int code, bool is_day) {
    int kind = IconFor(code, is_day);
    if (kind == icon_kind_) return;
    icon_kind_ = kind;

    for (auto* d : dots_) lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(block_, LV_OBJ_FLAG_HIDDEN);
    for (auto* ln : lines_) lv_obj_add_flag(ln, LV_OBJ_FLAG_HIDDEN);

    auto circle = [](lv_obj_t* o, int x, int y, int size, bool black) {
        lv_obj_set_pos(o, x, y);
        lv_obj_set_size(o, size, size);
        lv_obj_set_style_bg_color(o, black ? lv_color_black() : lv_color_white(), 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(o);
    };
    int nl = 0;
    auto line = [this, &nl](std::initializer_list<lv_point_precise_t> pts) {
        if (nl >= 4) return;
        int n = 0;
        for (auto& p : pts) {
            if (n < 3) line_pts_[nl][n++] = p;
        }
        lv_line_set_points(lines_[nl], line_pts_[nl], n);
        lv_obj_remove_flag(lines_[nl], LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(lines_[nl]);
        nl++;
    };
    auto cloud = [&](int dy) {
        circle(dots_[0], 2, 9 + dy, 10, false);
        circle(dots_[1], 8, 4 + dy, 13, false);
        lv_obj_set_pos(block_, 4, 12 + dy);
        lv_obj_set_size(block_, 18, 7);
        lv_obj_remove_flag(block_, LV_OBJ_FLAG_HIDDEN);
    };

    switch (kind) {
        case kIconSun:
            circle(dots_[0], 7, 7, 10, false);
            line({{12, 0}, {12, 3}});
            line({{12, 21}, {12, 24}});
            line({{0, 12}, {3, 12}});
            line({{21, 12}, {24, 12}});
            break;
        case kIconMoon:
            circle(dots_[0], 4, 4, 16, false);
            circle(dots_[1], 9, 1, 15, true);  // carve the crescent
            break;
        case kIconPartly:
            circle(dots_[2], 12, 1, 10, false);  // sun behind
            line({{21, 2}, {23, 0}});
            cloud(4);
            break;
        case kIconCloud:
            cloud(2);
            break;
        case kIconRain:
            cloud(-2);
            line({{7, 19}, {5, 23}});
            line({{13, 19}, {11, 23}});
            line({{19, 19}, {17, 23}});
            break;
        case kIconStorm:
            cloud(-2);
            line({{14, 17}, {10, 21}, {15, 21}});
            line({{15, 21}, {11, 24}});
            break;
        case kIconFog:
            line({{2, 7}, {22, 7}});
            line({{2, 12}, {22, 12}});
            line({{2, 17}, {22, 17}});
            break;
        default:
            break;
    }
    if (kind == kIconNone) {
        lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(time_label_, LV_ALIGN_TOP_MID, 0, 4);
    } else {
        lv_obj_remove_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(time_label_, LV_ALIGN_TOP_LEFT, 2, 4);
    }
}

// SSD1306 contrast: low during the night window (default 22:00-06:00)
void IdleScreen::UpdateContrast(int hour) {
    if (panel_io_ == nullptr) return;
    bool night = false;
    if (night_on_) {
        night = night_start_ <= night_end_ ? (hour >= night_start_ && hour < night_end_)
                                           : (hour >= night_start_ || hour < night_end_);
    }
    int want = night ? 0x08 : 0xCF;
    if (want == contrast_) return;
    uint8_t v = (uint8_t)want;
    if (esp_lcd_panel_io_tx_param(panel_io_, 0x81, &v, 1) == ESP_OK) {
        contrast_ = want;
        ESP_LOGI(TAG, "OLED contrast -> 0x%02X", want);
    }
}

std::string IdleScreen::LunarCountdown(int day, int month, int year) {
    auto today = lunar::SolarToLunar(day, month, year);
    if (today.day == 15) return "Hôm nay là Rằm";
    if (today.day == 1) return "Hôm nay mùng 1";
    struct tm base = {};
    base.tm_mday = day;
    base.tm_mon = month - 1;
    base.tm_year = year - 1900;
    base.tm_hour = 12;
    for (int i = 1; i <= 31; i++) {
        struct tm t = base;
        t.tm_mday += i;
        mktime(&t);
        auto ld = lunar::SolarToLunar(t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
        if (ld.day == 15 || ld.day == 1) {
            const char* what = ld.day == 15 ? "Rằm" : "mùng 1";
            if (i == 1) return std::string("Mai là ") + what;
            return "Còn " + std::to_string(i) + " ngày tới " + what;
        }
    }
    return "";
}

void IdleScreen::SetVisible(bool visible) {
    if (visible == visible_ || container_ == nullptr) return;
    if (visible) {
        lv_obj_remove_flag(container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(container_);
    } else {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
    visible_ = visible;
}

void IdleScreen::Tick() {
    tick_++;
    auto& app = Application::GetInstance();
    bool idle = app.GetDeviceState() == kDeviceStateIdle && !AlarmManager::GetInstance().IsRinging();
    idle_seconds_ = idle ? idle_seconds_ + 1 : 0;

    // Weather in the background: retry every minute until the first success,
    // then refresh every 30 minutes (only while idle)
    if (idle) {
        bool have = InfoServices::GetInstance().GetWeather().valid;
        if ((!have && tick_ % 60 == 5) || (have && tick_ % 1800 == 0)) {
            InfoServices::GetInstance().RefreshWeather(1800);
        }
    }

    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    bool time_ok = tmv.tm_year + 1900 >= 2024;
    bool want = time_ok && idle_seconds_ >= kShowAfterIdleSeconds && esp_timer_get_time() >= bump_until_us_;

    // Lunar countdown, recomputed once a day (outside the display lock)
    if (time_ok && tmv.tm_mday != countdown_day_) {
        countdown_day_ = tmv.tm_mday;
        countdown_cache_ = LunarCountdown(tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
    }

    if (display_ == nullptr) return;
    DisplayLockGuard lock(display_);
    if (container_ == nullptr) {
        Create();
    }
    if (time_ok) UpdateContrast(tmv.tm_hour);
    if (!want) {
        SetVisible(false);
        return;
    }

    char hhmm[32];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    lv_label_set_text(time_label_, hhmm);

    WeatherNow w = InfoServices::GetInstance().GetWeather();
    DrawIcon(w.valid ? w.code : -1, w.is_day);

    // Use the theme's text font (supports Vietnamese once assets are loaded)
    lv_obj_set_style_text_font(line_label_, lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN), 0);

    std::vector<std::string> lines;
    auto& radio = RadioPlayer::GetInstance();
    if (radio.IsPlaying()) lines.push_back("Radio: " + radio.CurrentName());
    {
        static const char* wd[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
        char date[48];
        snprintf(date, sizeof(date), "%s %02d/%02d", wd[tmv.tm_wday % 7], tmv.tm_mday, tmv.tm_mon + 1);
        auto ld = lunar::SolarToLunar(tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
        char lunar_buf[48];
        snprintf(lunar_buf, sizeof(lunar_buf), "%d/%d%s ÂL", ld.day, ld.month, ld.leap ? "N" : "");
        lines.push_back(std::string(date) + " · " + lunar_buf);
    }
    if (w.valid) {
        std::string t = std::to_string(w.temp) + "° " + w.desc;
        if (w.aqi >= 0) t += " · AQI " + std::to_string(w.aqi);
        lines.push_back(t);
    }
    if (!countdown_cache_.empty()) lines.push_back(countdown_cache_);

    const std::string& text = lines[(tick_ / kLineRotateSeconds) % lines.size()];
    if (text != lv_label_get_text(line_label_)) {
        lv_label_set_text(line_label_, text.c_str());
    }
    SetVisible(true);
}
