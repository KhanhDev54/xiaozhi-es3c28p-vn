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
#include "assets/lang_config.h"
#include "backlight.h"
#include "bank_store.h"
#include "board.h"
#include "display.h"
#include "ft6336_touch.h"
#include "info_services.h"
#include "lan_server.h"
#include "lunar_calendar.h"
#include "radio_player.h"
#include "touch_cal.h"
#include "vietqr.h"
#include "weather_icon.h"
#include "wifi_board.h"

#define TAG "Launcher"

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_28);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(font_material_symbols_30_4);

static const int W = 320;
static const int H = 240;
static const uint32_t kBg = 0x0B1220;
static const uint32_t kCard = 0x141C30;
static const uint32_t kCard2 = 0x1E293B;
static const uint32_t kText = 0xF1F5F9;
static const uint32_t kMuted = 0x94A3B8;
static const uint32_t kGold = 0xFBBF24;
static const uint32_t kAccent = 0x60A5FA;
static const uint32_t kGreen = 0x22C55E;
static const uint32_t kRed = 0xEF4444;
static const uint32_t kAmber = 0x9A7B4F;
static const int kReturnHomeSeconds = 6;
static const int kAutoHomeIdleMs = 60000;
static const int kTxPopupSeconds = 20;

static const char* kWeekdayLong[] = {"Chủ nhật", "Thứ hai", "Thứ ba", "Thứ tư", "Thứ năm", "Thứ sáu", "Thứ bảy"};

enum AppId {
    kAppChat, kAppPhoto, kAppMusic, kAppFm, kAppTimer, kAppNight, kAppQr, kAppHistory,
    kAppHud, kAppSystem, kAppSettings, kAppCount,
    kAppWifi = 100,  // opened from the control panel, not in the menu
};

enum IconKind { kIconMaterial, kIconSymbol, kIconMoon, kIconQr };

struct AppInfo {
    const char* name;
    IconKind kind;
    const char* icon;
    uint32_t c1, c2;   // gradient
};

static const AppInfo kApps[kAppCount] = {
    {"Chat", kIconMaterial, MATERIAL_SYMBOLS_CHAT_BUBBLE, 0x818CF8, 0x4F46E5},
    {"Ảnh", kIconSymbol, LV_SYMBOL_IMAGE, 0xFDE047, 0xF59E0B},
    {"Nhạc", kIconSymbol, LV_SYMBOL_AUDIO, 0xFB7185, 0xE11D48},
    {"FM", kIconMaterial, MATERIAL_SYMBOLS_HEADPHONES, 0xE879F9, 0xA21CAF},
    {"Hẹn giờ", kIconMaterial, MATERIAL_SYMBOLS_ALARM, 0xFDBA74, 0xF97316},
    {"Ban đêm", kIconMoon, nullptr, 0x3730A3, 0x312E81},
    {"QR", kIconQr, nullptr, 0x4ADE80, 0x16A34A},
    {"Lịch sử", kIconSymbol, LV_SYMBOL_LIST, 0x7DD3FC, 0x0284C7},
    {"Waze HUD", kIconSymbol, LV_SYMBOL_GPS, 0x67E8F9, 0x0891B2},
    {"Hệ thống", kIconMaterial, MATERIAL_SYMBOLS_MEMORY, 0x9CA3AF, 0x4B5563},
    {"Cài đặt", kIconMaterial, MATERIAL_SYMBOLS_SETTINGS, 0xD1D5DB, 0x6B7280},
};

static const uint32_t kLedPalette[] = {0x000000, 0xEF4444, 0xF97316, 0xFACC15, 0x22C55E, 0x22D3EE, 0x3B82F6, 0xA855F7};

int Launcher::LedColorCount() { return (int)(sizeof(kLedPalette) / sizeof(kLedPalette[0])); }
uint32_t Launcher::LedColor(int index) {
    if (index < 0 || index >= LedColorCount()) return 0;
    return kLedPalette[index];
}

static uint32_t BankColor(const std::string& bank) {
    if (bank == "Vietcombank") return 0x16A34A;
    if (bank == "Techcombank") return 0xDC2626;
    if (bank == "MB Bank") return 0x1D4ED8;
    if (bank == "BIDV") return 0x0E7490;
    if (bank == "VietinBank") return 0x1E40AF;
    if (bank == "Agribank") return 0x9F1239;
    if (bank == "ACB") return 0x2563EB;
    if (bank == "TPBank") return 0x7E22CE;
    if (bank == "MoMo") return 0xBE185D;
    return 0x475569;
}

static std::string Vnd(int64_t v, bool sign) {
    std::string s = FormatThousands(v) + "đ";
    if (!sign) return s;
    return (v < 0 ? "-" : "+") + s;
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

static struct tm Now() {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    return tmv;
}

static bool TimeSynced() { return Now().tm_year + 1900 >= 2024; }

static int64_t DayStart(int64_t ts) {
    time_t t = (time_t)ts;
    struct tm tmv;
    localtime_r(&t, &tmv);
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    return (int64_t)mktime(&tmv);
}

// First second of the month that is `back` months before the current one
static int64_t MonthStart(int back) {
    struct tm tmv = Now();
    tmv.tm_mday = 1;
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    tmv.tm_mon -= back;
    return (int64_t)mktime(&tmv);
}

static std::string Hm(int64_t ts) {
    time_t t = (time_t)ts;
    struct tm tmv;
    localtime_r(&t, &tmv);
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return b;
}

static std::string WhenText(int64_t ts) {
    time_t t = (time_t)ts;
    struct tm tmv;
    localtime_r(&t, &tmv);
    char b[40];
    if (DayStart(ts) == DayStart(time(nullptr))) {
        snprintf(b, sizeof(b), "%02d:%02d · hôm nay", tmv.tm_hour, tmv.tm_min);
    } else {
        snprintf(b, sizeof(b), "%02d:%02d · %02d/%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_mday, tmv.tm_mon + 1);
    }
    return b;
}

// "0123456789" -> "0123 4567 89"
static std::string GroupDigits(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (i > 0 && i % 4 == 0) o += ' ';
        o += s[i];
    }
    return o;
}


// QR codes: LVGL's own qrcode lib clashes with the copy of qrcodegen linked by esp_emote_gfx,
// so use that copy directly (Nayuki qrcodegen C API) and draw into a canvas.
extern "C" {
bool qrcodegen_encodeText(const char* text, uint8_t tempBuffer[], uint8_t qrcode[], int ecl, int minVersion,
                          int maxVersion, int mask, bool boostEcl);
int qrcodegen_getSize(const uint8_t qrcode[]);
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);
}

static lv_obj_t* QrCanvas(lv_obj_t* parent, const std::string& text, int size, lv_color_t dark, lv_color_t light) {
    const int kMaxVersion = 15;
    const size_t kLen = (size_t)((kMaxVersion * 4 + 17) * (kMaxVersion * 4 + 17) + 7) / 8 + 1;
    std::vector<uint8_t> qr(kLen), tmp(kLen);
    lv_obj_t* canvas = lv_canvas_create(parent);
    void* buf = heap_caps_malloc((size_t)size * size * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == nullptr) return canvas;
    lv_canvas_set_buffer(canvas, buf, size, size, LV_COLOR_FORMAT_RGB565);
    lv_obj_add_event_cb(canvas, [](lv_event_t* e) {
        heap_caps_free(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, buf);
    lv_canvas_fill_bg(canvas, light, LV_OPA_COVER);
    if (!qrcodegen_encodeText(text.c_str(), tmp.data(), qr.data(), 1 /* MEDIUM */, 1, kMaxVersion,
                              -1 /* AUTO */, true)) {
        ESP_LOGW(TAG, "QR encode failed (%u bytes)", (unsigned)text.size());
        return canvas;
    }
    int n = qrcodegen_getSize(qr.data());
    int scale = size / n;
    if (scale < 1) scale = 1;
    int off = (size - n * scale) / 2;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            if (!qrcodegen_getModule(qr.data(), x, y)) continue;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    int px = off + x * scale + dx, py = off + y * scale + dy;
                    if (px < size && py < size) lv_canvas_set_px(canvas, px, py, dark, LV_OPA_COVER);
                }
            }
        }
    }
    return canvas;
}

// ---------------------------------------------------------------- lifecycle

void Launcher::Start(Display* display, Backlight* backlight) {
    display_ = display;
    backlight_ = backlight;
    touch_cal::Load();
    BankStore::GetInstance().Load();
    auto s = BankStore::GetInstance().Settings();
    touch_cal::SetFlip(s.rotate180);
    if (rotate_ && s.rotate180) rotate_(true);
    if (led_) led_(s.led_color);
    BankStore::GetInstance().SetListener([this](const BankTx&) { tx_pending_ = true; });
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
        }, "launcher_job", 12288, heap_fn, 3, nullptr) != pdPASS) {
        delete heap_fn;
    }
}

bool Launcher::AutoHomeAllowed() const {
    if (!BankStore::GetInstance().Settings().auto_home) return false;
    if (mode_ == Mode::kMenu || mode_ == Mode::kControl) return true;
    if (mode_ == Mode::kApp) return app_id_ != kAppQr && app_id_ != kAppFm;
    return false;
}

void Launcher::Tick() {
    tick_++;
    auto& app = Application::GetInstance();
    auto& am = AlarmManager::GetInstance();
    auto state = app.GetDeviceState();
    bool idle = state == kDeviceStateIdle;
    int64_t now_us = esp_timer_get_time();
    if (!idle) busy_until_us_ = now_us + (int64_t)kReturnHomeSeconds * 1000000;

    // Background work (only while idle)
    if (idle) {
        bool have = InfoServices::GetInstance().GetWeather().valid;
        if ((!have && tick_ % 60 == 3) || (have && tick_ % 1800 == 0)) {
            InfoServices::GetInstance().RefreshWeather(1800);
        }
    }
    if (tick_ % 5 == 0) LanServer::GetInstance().EnsureStarted();

    DisplayLockGuard lock(display_);
    if (root_ == nullptr) {
        Build();
        if (!touch_cal::IsCalibrated()) {
            StartCalibration();
        }
    }
    if (mode_ == Mode::kCalibrate) return;

    if (!idle && !am.IsRinging()) {
        if (mode_ != Mode::kHidden) {
            if (mode_ == Mode::kOverlay) CloseOverlay();
            Show(Mode::kHidden);
        }
        return;
    }

    // Alarm ringing takes over the screen
    if (am.IsRinging()) {
        if (overlay_ != Overlay::kRing) ShowOverlay(Overlay::kRing);
        UpdatePill();
        return;
    } else if (overlay_ == Overlay::kRing) {
        CloseOverlay();
    }

    if (mode_ == Mode::kHidden && now_us >= busy_until_us_) {
        Show(Mode::kHome);
    }

    // New bank transaction
    if (tx_pending_.exchange(false)) HandleNewTx();

    // Night screen
    bool night = am.IsNightActive() || now_us < night_wake_until_us_;
    if (night && overlay_ != Overlay::kNight && overlay_ != Overlay::kTx && overlay_ != Overlay::kReceived &&
        mode_ != Mode::kHidden) {
        ShowOverlay(Overlay::kNight);
    } else if (!night && overlay_ == Overlay::kNight) {
        CloseOverlay();
    } else if (overlay_ == Overlay::kNight && tick_ % 20 == 0) {
        BuildNightOverlay();
    }

    // Overlay timeout
    if (mode_ == Mode::kOverlay && overlay_until_us_ != 0 && now_us >= overlay_until_us_) {
        CloseOverlay();
    }

    // Return to the clock after a minute without touches
    if (AutoHomeAllowed() && lv_display_get_inactive_time(nullptr) > (uint32_t)kAutoHomeIdleMs) {
        Show(Mode::kHome);
    }

    if (mode_ == Mode::kHome) {
        UpdateHome(false);
        if (bank_rev_shown_ != BankStore::GetInstance().Revision() || tick_ % 30 == 0) UpdateBankPage();
    }
    struct tm tmv = Now();
    if (mode_ == Mode::kMenu && menu_time_) {
        lv_label_set_text_fmt(menu_time_, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    }
    if (mode_ == Mode::kApp) {
        if (app_time_) lv_label_set_text_fmt(app_time_, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        RefreshApp();
    }
    UpdatePill();
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
    lv_obj_add_flag(p, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
    lv_obj_add_event_cb(p, OnGesture, LV_EVENT_GESTURE, this);
    return p;
}

static lv_obj_t* Card(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color = kCard) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_clear_flag(c, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return c;
}

lv_obj_t* Launcher::Chip(lv_obj_t* parent, const char* text, uint32_t bg, uint32_t fg, const lv_font_t* font) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 8, 0);
    lv_obj_set_style_pad_ver(c, 1, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_clear_flag(c, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    Label(c, font, fg, text);
    return c;
}

lv_obj_t* Launcher::Button(lv_obj_t* parent, int x, int y, int w, int h, const char* text, uint32_t bg,
                           uint32_t fg, lv_event_cb_t cb, void* user) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 2, 0);
    lv_obj_t* l = Label(b, nullptr, fg, text);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user ? user : this);
    return b;
}

lv_obj_t* Launcher::StatusBar(lv_obj_t* parent, const char* left_text, lv_obj_t** time_label) {
    struct tm tmv = Now();
    char date[48];
    if (left_text == nullptr) {
        snprintf(date, sizeof(date), "%s, %02d/%02d", kWeekdayLong[tmv.tm_wday % 7], tmv.tm_mday, tmv.tm_mon + 1);
        left_text = date;
    }
    lv_obj_t* left = Label(parent, nullptr, kText, left_text);
    lv_obj_set_pos(left, 10, 3);
    lv_obj_t* wifi = Label(parent, f14_, WifiManager::GetInstance().IsConnected() ? kText : 0x475569, LV_SYMBOL_WIFI);
    lv_obj_align(wifi, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_t* t = Label(parent, f14_, kText, "");
    lv_label_set_text_fmt(t, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    lv_obj_align(t, LV_ALIGN_TOP_RIGHT, -30, 8);
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, W, 1);
    lv_obj_set_pos(line, 0, 29);
    lv_obj_set_style_bg_color(line, lv_color_hex(kCard2), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    if (time_label) *time_label = t;
    return left;
}

static void SetDots(lv_obj_t** dots, int n, int active) {
    for (int i = 0; i < n; i++) {
        if (!dots[i]) continue;
        lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == active ? kAccent : 0x475569), 0);
        lv_obj_set_width(dots[i], i == active ? 12 : 5);
    }
}

static void MakeDots(lv_obj_t* parent, lv_obj_t** dots, int n, int y) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 80, 6);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    for (int i = 0; i < n; i++) {
        lv_obj_t* d = lv_obj_create(row);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 5, 5);
        lv_obj_set_style_radius(d, 3, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
        dots[i] = d;
    }
    SetDots(dots, n, 0);
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
            if (dir == LV_DIR_TOP) {
                self->Show(Mode::kMenu);
            } else if (dir == LV_DIR_BOTTOM) {
                self->Show(Mode::kControl);
            } else if (self->app_id_ == kAppQr && (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)) {
                int n = (int)BankStore::GetInstance().Accounts().size();
                if (n <= 1) return;
                self->app_page_ = (self->app_page_ + (dir == LV_DIR_LEFT ? 1 : n - 1)) % n;
                self->OpenApp(kAppQr);
            } else {
                return;
            }
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
    // Montserrat has no Vietnamese letters: fall back to the (20 px) text font for them
    static lv_font_t f14 = lv_font_montserrat_14;
    static lv_font_t f28 = lv_font_montserrat_28;
    f14.fallback = text_font_;
    f28.fallback = text_font_;
    f14_ = &f14;
    f28_ = &f28;

    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, W, H);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    BuildHome();
    BuildMenu();
    BuildControl();
    app_panel_ = NewPanel();
    overlay_panel_ = NewPanel();
    BuildPill();
    tx_rev_seen_ = BankStore::GetInstance().Revision();
    ESP_LOGI(TAG, "UI built");
}

void Launcher::BuildHome() {
    home_ = lv_tileview_create(root_);
    lv_obj_set_size(home_, W, H);
    lv_obj_set_style_bg_color(home_, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(home_, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(home_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(home_, OnGesture, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(home_, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->UpdateHomeDots();
    }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);

    // ---- page 1: clock + weather
    lv_obj_t* t1 = lv_tileview_add_tile(home_, 0, 0, LV_DIR_RIGHT);
    lv_obj_add_event_cb(t1, OnGesture, LV_EVENT_GESTURE, this);

    date_lbl_ = Label(t1, nullptr, kText, "");
    lv_obj_set_pos(date_lbl_, 10, 4);

    aqi_chip_ = lv_obj_create(t1);
    lv_obj_remove_style_all(aqi_chip_);
    lv_obj_set_size(aqi_chip_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(aqi_chip_, 8, 0);
    lv_obj_set_style_pad_ver(aqi_chip_, 1, 0);
    lv_obj_set_style_radius(aqi_chip_, 10, 0);
    lv_obj_set_style_bg_opa(aqi_chip_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(aqi_chip_, LV_OBJ_FLAG_CLICKABLE);
    aqi_lbl_ = Label(aqi_chip_, nullptr, 0x0B1220, "");
    lv_obj_align(aqi_chip_, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_add_flag(aqi_chip_, LV_OBJ_FLAG_HIDDEN);

    time_lbl_ = Label(t1, &lv_font_montserrat_48, kText, "--:--");
    lv_obj_set_pos(time_lbl_, 8, 30);
    sec_lbl_ = Label(t1, f28_, kMuted, "");
    lv_obj_align_to(sec_lbl_, time_lbl_, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);

    icon_ = new WeatherIcon(t1, 48);
    lv_obj_set_pos(icon_->obj(), 196, 36);
    temp_lbl_ = Label(t1, f28_, kText, "");
    lv_obj_set_pos(temp_lbl_, 252, 46);
    desc_lbl_ = Label(t1, nullptr, kMuted, "");
    lv_obj_set_width(desc_lbl_, 124);
    lv_label_set_long_mode(desc_lbl_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(desc_lbl_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(desc_lbl_, 190, 88);

    lunar_lbl_ = Label(t1, nullptr, kGold, "");
    lv_obj_set_width(lunar_lbl_, 180);
    lv_label_set_long_mode(lunar_lbl_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(lunar_lbl_, 10, 88);
    city_lbl_ = Label(t1, nullptr, kMuted, "");
    lv_obj_set_width(city_lbl_, 180);
    lv_label_set_long_mode(city_lbl_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(city_lbl_, 10, 112);

    detail_lbl_ = Label(t1, nullptr, kText, "");
    lv_obj_set_width(detail_lbl_, W - 20);
    lv_label_set_long_mode(detail_lbl_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(detail_lbl_, 10, 138);

    for (int i = 0; i < 4; i++) {
        lv_obj_t* c = Card(t1, 6 + i * 78, 166, 74, 62);
        fc_day_[i] = Label(c, nullptr, kText, "");
        lv_obj_set_pos(fc_day_[i], 6, 1);
        fc_icon_[i] = new WeatherIcon(c, 22);
        lv_obj_set_pos(fc_icon_[i]->obj(), 46, 4);
        fc_range_[i] = Label(c, f14_, kText, "");
        lv_obj_set_pos(fc_range_[i], 6, 26);
        fc_rain_[i] = Label(c, f14_, kAccent, "");
        lv_obj_set_pos(fc_rain_[i], 6, 42);
    }

    // ---- page 2: month calendar
    lv_obj_t* t2 = lv_tileview_add_tile(home_, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_add_event_cb(t2, OnGesture, LV_EVENT_GESTURE, this);
    lv_obj_t* left = Card(t2, 6, 6, 112, 222);
    cal_big_ = Label(left, &lv_font_montserrat_48, kGold, "");
    lv_obj_set_pos(cal_big_, 8, 4);
    cal_info_ = Label(left, nullptr, kText, "");
    lv_obj_set_width(cal_info_, 100);
    lv_obj_set_height(cal_info_, 156);
    lv_label_set_long_mode(cal_info_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(cal_info_, 8, 60);

    cal_title_ = Label(t2, nullptr, kText, "");
    lv_obj_set_pos(cal_title_, 126, 4);
    static const char* kHead[] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};
    const int gx = 124, cw = 27, gy = 30;
    for (int c = 0; c < 7; c++) {
        lv_obj_t* h = Label(t2, f14_, c == 6 ? 0xF87171 : kMuted, kHead[c]);
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
        lv_obj_clear_flag(cell, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_t* d = Label(cell, f14_, kText, "");
        lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_t* l = Label(cell, f14_, kMuted, "");
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, 2);
        cal_cells_[i] = cell;
        cal_lunar_[i] = l;
        lv_obj_set_user_data(cell, d);
    }

    // ---- page 3: Biến động số dư
    lv_obj_t* t3 = lv_tileview_add_tile(home_, 2, 0, LV_DIR_LEFT);
    lv_obj_add_event_cb(t3, OnGesture, LV_EVENT_GESTURE, this);
    bank_page_ = t3;
    lv_obj_t* title = Label(t3, nullptr, kText, "Biến động số dư");
    lv_obj_set_pos(title, 10, 3);
    bank_read_ = Label(t3, nullptr, kMuted, "");
    lv_obj_align(bank_read_, LV_ALIGN_TOP_RIGHT, -10, 3);
    bank_body_ = lv_obj_create(t3);
    lv_obj_remove_style_all(bank_body_);
    lv_obj_set_size(bank_body_, W, 200);
    lv_obj_set_pos(bank_body_, 0, 30);
    lv_obj_clear_flag(bank_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bank_body_, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // dots live outside the tileview so they stay put while swiping
    MakeDots(root_, home_dots_, 3, 232);
    lv_obj_add_flag(lv_obj_get_parent(home_dots_[0]), LV_OBJ_FLAG_HIDDEN);
}

static void DrawIcon(lv_obj_t* tile, const AppInfo& a, const lv_font_t* f28) {
    switch (a.kind) {
        case kIconMaterial: {
            lv_obj_t* ic = lv_label_create(tile);
            lv_obj_set_style_text_font(ic, &font_material_symbols_30_4, 0);
            lv_obj_set_style_text_color(ic, lv_color_white(), 0);
            lv_label_set_text(ic, a.icon);
            lv_obj_center(ic);
            break;
        }
        case kIconSymbol: {
            lv_obj_t* ic = lv_label_create(tile);
            lv_obj_set_style_text_font(ic, f28, 0);
            lv_obj_set_style_text_color(ic, lv_color_white(), 0);
            lv_label_set_text(ic, a.icon);
            lv_obj_center(ic);
            break;
        }
        case kIconMoon: {
            lv_obj_t* moon = lv_obj_create(tile);
            lv_obj_remove_style_all(moon);
            lv_obj_set_size(moon, 28, 28);
            lv_obj_set_style_radius(moon, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(moon, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(moon, LV_OPA_COVER, 0);
            lv_obj_align(moon, LV_ALIGN_CENTER, -3, 0);
            lv_obj_t* cut = lv_obj_create(tile);
            lv_obj_remove_style_all(cut);
            lv_obj_set_size(cut, 24, 24);
            lv_obj_set_style_radius(cut, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(cut, lv_color_hex(a.c1), 0);
            lv_obj_set_style_bg_opa(cut, LV_OPA_COVER, 0);
            lv_obj_align(cut, LV_ALIGN_CENTER, 7, -6);
            break;
        }
        case kIconQr: {
            static const char* kData = "KenDev";
            lv_obj_t* q = QrCanvas(tile, kData, 30, lv_color_white(), lv_color_hex(a.c1));
            lv_obj_center(q);
            break;
        }
    }
}

void Launcher::BuildMenu() {
    menu_ = NewPanel();
    menu_date_ = StatusBar(menu_, nullptr, &menu_time_);

    menu_tiles_ = lv_tileview_create(menu_);
    lv_obj_set_size(menu_tiles_, W, 184);
    lv_obj_set_pos(menu_tiles_, 0, 32);
    lv_obj_set_style_bg_opa(menu_tiles_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(menu_tiles_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(menu_tiles_, OnGesture, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(menu_tiles_, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->UpdateMenuDots();
    }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_t* pages[2] = {
        lv_tileview_add_tile(menu_tiles_, 0, 0, LV_DIR_RIGHT),
        lv_tileview_add_tile(menu_tiles_, 1, 0, LV_DIR_LEFT),
    };
    for (auto* p : pages) lv_obj_add_event_cb(p, OnGesture, LV_EVENT_GESTURE, this);

    for (int i = 0; i < kAppCount; i++) {
        int page = i / 8, slot = i % 8;
        int col = slot % 4, row = slot / 4;
        lv_obj_t* btn = lv_obj_create(pages[page]);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, 76, 90);
        lv_obj_set_pos(btn, 4 + col * 79, 2 + row * 92);
        lv_obj_add_flag(btn, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_opa(btn, LV_OPA_60, LV_STATE_PRESSED);

        lv_obj_t* tile = lv_obj_create(btn);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 56, 56);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 2);
        lv_obj_set_style_radius(tile, 15, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(kApps[i].c1), 0);
        lv_obj_set_style_bg_grad_color(tile, lv_color_hex(kApps[i].c2), 0);
        lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
        lv_obj_clear_flag(tile, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        DrawIcon(tile, kApps[i], f28_);

        lv_obj_t* name = Label(btn, nullptr, kText, kApps[i].name);
        lv_obj_set_width(name, 76);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 62);

        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
            self->app_page_ = 0;
            self->OpenApp(id);
        }, LV_EVENT_CLICKED, this);
    }
    MakeDots(menu_, menu_dots_, 2, 214);
    lv_obj_t* hint = Label(menu_, nullptr, kMuted, "Vuốt xuống để đóng");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void Launcher::UpdateMenuDots() {
    lv_obj_t* act = lv_tileview_get_tile_active(menu_tiles_);
    SetDots(menu_dots_, 2, act ? lv_obj_get_x(act) / W : 0);
}

void Launcher::UpdateHomeDots() {
    lv_obj_t* act = lv_tileview_get_tile_active(home_);
    int page = act ? lv_obj_get_x(act) / W : 0;
    SetDots(home_dots_, 3, page);
    if (page == 2) {
        BankStore::GetInstance().MarkRead();
        UpdateBankPage();
    }
}

void Launcher::BuildControl() {
    ctrl_ = NewPanel();
    lv_obj_t* title = Label(ctrl_, nullptr, kText, "Điều khiển");
    lv_obj_set_pos(title, 12, 4);
    lv_obj_t* hint = Label(ctrl_, nullptr, kMuted, "Vuốt lên để đóng");
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -12, 4);

    auto make_slider = [this](int y, const char* icon, lv_obj_t** slider, lv_obj_t** value) {
        lv_obj_t* ic = Label(ctrl_, &font_material_symbols_30_4, kAccent, icon);
        lv_obj_set_pos(ic, 10, y - 8);
        lv_obj_t* s = lv_slider_create(ctrl_);
        lv_obj_set_size(s, 196, 10);
        lv_obj_set_pos(s, 54, y + 6);
        lv_slider_set_range(s, 0, 100);
        lv_obj_set_style_bg_color(s, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s, lv_color_hex(0x3B82F6), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t* v = Label(ctrl_, f14_, kText, "");
        lv_obj_set_pos(v, 266, y + 2);
        *slider = s;
        *value = v;
    };
    make_slider(40, MATERIAL_SYMBOLS_VOLUME_UP, &vol_slider_, &vol_lbl_);
    make_slider(78, MATERIAL_SYMBOLS_BRIGHTNESS_6, &bri_slider_, &bri_lbl_);

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

    // LED colours
    lv_obj_t* led = Label(ctrl_, nullptr, kMuted, "Đèn LED");
    lv_obj_set_pos(led, 10, 108);
    for (int i = 0; i < LedColorCount() && i < 8; i++) {
        lv_obj_t* d = lv_obj_create(ctrl_);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 22, 22);
        lv_obj_set_pos(d, 96 + i * 27, 108);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(i == 0 ? 0x334155 : kLedPalette[i]), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(d, lv_color_white(), 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_ext_click_area(d, 3);
        lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(d, (void*)(intptr_t)i);
        lv_obj_add_event_cb(d, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
            auto s = BankStore::GetInstance().Settings();
            s.led_color = idx;
            BankStore::GetInstance().SaveSettings(s);
            if (self->led_) self->led_(idx);
            self->UpdateControl();
        }, LV_EVENT_CLICKED, this);
        led_dots_[i] = d;
    }

    // Toggles
    auto make_toggle = [this](int x, const char* text, lv_event_cb_t cb) {
        lv_obj_t* b = Button(ctrl_, x, 140, 148, 36, text, kCard2, kText, cb);
        return b;
    };
    tog_home_ = make_toggle(8, "Tự về đồng hồ", [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        auto s = BankStore::GetInstance().Settings();
        s.auto_home = !s.auto_home;
        BankStore::GetInstance().SaveSettings(s);
        self->UpdateControl();
    });
    tog_read_ = make_toggle(164, "Đọc giao dịch", [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        auto s = BankStore::GetInstance().Settings();
        s.read_tx = !s.read_tx;
        BankStore::GetInstance().SaveSettings(s);
        self->UpdateControl();
    });

    // Buttons
    Button(ctrl_, 8, 188, 96, 44, "Xoay 180°", kCard2, kText, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        auto s = BankStore::GetInstance().Settings();
        s.rotate180 = !s.rotate180;
        BankStore::GetInstance().SaveSettings(s);
        touch_cal::SetFlip(s.rotate180);
        if (self->rotate_) self->rotate_(s.rotate180);
    });
    Button(ctrl_, 110, 188, 80, 44, "Wi-Fi", kCard2, kText, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->OpenApp(kAppWifi);
    });
    Button(ctrl_, 196, 188, 116, 44, "Khởi động lại", kCard2, 0xF87171, [](lv_event_t*) { esp_restart(); });
}

void Launcher::UpdateControl() {
    auto s = BankStore::GetInstance().Settings();
    for (int i = 0; i < 8; i++) {
        if (led_dots_[i]) lv_obj_set_style_border_width(led_dots_[i], i == s.led_color ? 3 : 0, 0);
    }
    lv_obj_set_style_bg_color(tog_home_, lv_color_hex(s.auto_home ? 0x2563EB : kCard2), 0);
    lv_obj_set_style_bg_color(tog_read_, lv_color_hex(s.read_tx ? 0x2563EB : kCard2), 0);
}

void Launcher::BuildPill() {
    pill_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(pill_);
    lv_obj_set_size(pill_, LV_SIZE_CONTENT, 20);
    lv_obj_set_style_pad_hor(pill_, 10, 0);
    lv_obj_set_style_radius(pill_, 10, 0);
    lv_obj_set_style_bg_color(pill_, lv_color_hex(0xDB2777), 0);
    lv_obj_set_style_bg_opa(pill_, LV_OPA_COVER, 0);
    lv_obj_add_flag(pill_, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
    lv_obj_clear_flag(pill_, LV_OBJ_FLAG_SCROLLABLE);
    pill_lbl_ = Label(pill_, f14_, 0xFFFFFF, "");
    lv_obj_center(pill_lbl_);
    lv_obj_align(pill_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_event_cb(pill_, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        if (AlarmManager::GetInstance().TimerRemaining() > 0) self->OpenApp(kAppTimer);
        else self->OpenApp(kAppFm);
    }, LV_EVENT_CLICKED, this);
}

void Launcher::UpdatePill() {
    if (!pill_) return;
    bool show_mode = mode_ == Mode::kHome || mode_ == Mode::kMenu || mode_ == Mode::kControl ||
                     (mode_ == Mode::kApp && app_id_ != kAppFm && app_id_ != kAppTimer);
    int remain = AlarmManager::GetInstance().TimerRemaining();
    bool playing = RadioPlayer::GetInstance().IsPlaying();
    if (!show_mode || (remain <= 0 && !playing)) {
        lv_obj_add_flag(pill_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (remain > 0) {
        lv_obj_set_style_bg_color(pill_, lv_color_hex(0xEA580C), 0);
        lv_label_set_text_fmt(pill_lbl_, LV_SYMBOL_BELL " %02d:%02d", remain / 60, remain % 60);
    } else {
        lv_obj_set_style_bg_color(pill_, lv_color_hex(0xDB2777), 0);
        std::string name = RadioPlayer::GetInstance().CurrentName();
        if (name.size() > 18) name = name.substr(0, 18);
        lv_label_set_text_fmt(pill_lbl_, LV_SYMBOL_AUDIO " %s", name.c_str());
    }
    lv_obj_remove_flag(pill_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pill_);
}

// ---------------------------------------------------------------- navigation

void Launcher::Show(Mode mode) {
    if (root_ == nullptr) return;
    lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lv_obj_get_parent(home_dots_[0]), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(app_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctrl_, LV_OBJ_FLAG_HIDDEN);
    if (mode != Mode::kOverlay) {
        lv_obj_add_flag(overlay_panel_, LV_OBJ_FLAG_HIDDEN);
        overlay_ = Overlay::kNone;
    }
    if (mode != Mode::kApp && mode != Mode::kOverlay) {
        app_id_ = -1;
        app_time_ = nullptr;
        app_dynamic_ = nullptr;
    }
    mode_ = mode;

    if (mode == Mode::kHidden) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        UpdatePill();
        return;
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);

    struct tm tmv = Now();
    switch (mode) {
        case Mode::kHome:
            lv_obj_remove_flag(home_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(lv_obj_get_parent(home_dots_[0]), LV_OBJ_FLAG_HIDDEN);
            UpdateHome(true);
            UpdateBankPage();
            break;
        case Mode::kMenu: {
            lv_label_set_text_fmt(menu_date_, "%s, %02d/%02d", kWeekdayLong[tmv.tm_wday % 7], tmv.tm_mday,
                                  tmv.tm_mon + 1);
            lv_label_set_text_fmt(menu_time_, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            lv_obj_remove_flag(menu_, LV_OBJ_FLAG_HIDDEN);
            UpdateMenuDots();
            break;
        }
        case Mode::kApp:
            lv_obj_remove_flag(app_panel_, LV_OBJ_FLAG_HIDDEN);
            break;
        case Mode::kOverlay:
            lv_obj_remove_flag(overlay_panel_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(overlay_panel_);
            break;
        case Mode::kControl: {
            int vol = Board::GetInstance().GetAudioCodec()->output_volume();
            int bri = backlight_ ? backlight_->brightness() : 100;
            lv_slider_set_value(vol_slider_, vol, LV_ANIM_OFF);
            lv_slider_set_value(bri_slider_, bri, LV_ANIM_OFF);
            lv_label_set_text_fmt(vol_lbl_, "%d%%", vol);
            lv_label_set_text_fmt(bri_lbl_, "%d%%", bri);
            UpdateControl();
            lv_obj_remove_flag(ctrl_, LV_OBJ_FLAG_HIDDEN);
            break;
        }
        default:
            break;
    }
    UpdatePill();
}

// ---------------------------------------------------------------- home pages

void Launcher::UpdateHome(bool full) {
    struct tm tmv = Now();
    if (!TimeSynced()) {
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
            lv_label_set_text_fmt(aqi_lbl_, "AQI %d · %s", w.aqi, AqiText(w.aqi));
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
    struct tm tmv = Now();
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

void Launcher::UpdateBankPage() {
    auto& store = BankStore::GetInstance();
    bank_rev_shown_ = store.Revision();
    lv_obj_clean(bank_body_);
    lv_label_set_text(bank_read_, store.unread() ? "Mới" : "Đã đọc");
    lv_obj_set_style_text_color(bank_read_, lv_color_hex(store.unread() ? kGreen : kMuted), 0);

    auto list = store.List(3);
    if (list.empty()) {
        lv_obj_t* l = Label(bank_body_, nullptr, kMuted,
                            "Chưa có giao dịch.\nMở Menu → Cài đặt để nối báo chuyển khoản qua mạng Wi-Fi.");
        lv_obj_set_width(l, W - 24);
        lv_obj_set_pos(l, 12, 10);
        return;
    }
    const BankTx& t = list[0];
    bool in = t.amount >= 0;
    lv_obj_t* card = Card(bank_body_, 6, 0, W - 12, 120);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(in ? kGreen : kRed), 0);
    lv_obj_add_flag(card, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_add_event_cb(card, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        BankTx tx;
        if (BankStore::GetInstance().Latest(tx)) {
            self->BuildTxOverlay(tx, true);
            self->ShowOverlay(Overlay::kTxDetail);
        }
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* chip = Chip(card, t.bank.c_str(), BankColor(t.bank), 0xFFFFFF, f14_);
    lv_obj_set_pos(chip, 8, 6);
    if (!t.account.empty()) {
        lv_obj_t* acc = Label(card, f14_, kMuted, ("TK *" + t.account).c_str());
        lv_obj_align_to(acc, chip, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    }
    lv_obj_t* when = Label(card, f14_, kMuted, Hm(t.ts).c_str());
    lv_obj_align(when, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_t* amt = Label(card, f28_, in ? kGreen : kRed, Vnd(t.amount, true).c_str());
    lv_obj_set_pos(amt, 8, 26);
    std::string who = in ? (t.name.empty() ? t.content : "Từ " + t.name) : (t.content.empty() ? "Chuyển đi" : t.content);
    lv_obj_t* name = Label(card, nullptr, kText, who.c_str());
    lv_obj_set_width(name, 170);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(name, 8, 62);
    if (t.balance >= 0) {
        lv_obj_t* bal = Label(card, f14_, kMuted, ("SD " + Vnd(t.balance, false)).c_str());
        lv_obj_align(bal, LV_ALIGN_TOP_RIGHT, -8, 66);
    }
    lv_obj_t* raw = Label(card, nullptr, kMuted, t.raw.c_str());
    lv_obj_set_width(raw, W - 32);
    lv_label_set_long_mode(raw, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(raw, 8, 90);

    int64_t in_sum, out_sum;
    int count;
    int64_t today = DayStart(time(nullptr));
    store.Totals(today, today + 86400 * 2, in_sum, out_sum, count);
    lv_obj_t* s_in = Label(bank_body_, nullptr, kGreen, ("Nhận " + Vnd(in_sum, false)).c_str());
    lv_obj_set_pos(s_in, 10, 123);
    lv_obj_t* s_out = Label(bank_body_, nullptr, 0xF87171, ("Chi " + Vnd(out_sum, false)).c_str());
    lv_obj_align(s_out, LV_ALIGN_TOP_RIGHT, -10, 123);

    for (size_t i = 1; i < list.size() && i < 3; i++) {
        const BankTx& r = list[i];
        std::string line = Hm(r.ts) + "  " + Vnd(r.amount, true) + "  " + r.bank;
        lv_obj_t* l = Label(bank_body_, nullptr, r.amount >= 0 ? kGreen : 0xF87171, line.c_str());
        lv_obj_set_width(l, W - 20);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(l, 10, 146 + (int)(i - 1) * 24);
    }
}

// ---------------------------------------------------------------- bank events

void Launcher::HandleNewTx() {
    BankTx tx;
    if (!BankStore::GetInstance().Latest(tx)) return;
    auto s = BankStore::GetInstance().Settings();
    if (s.read_tx) {
        Application::GetInstance().Schedule([]() { Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP); });
    }
    AlarmManager::GetInstance().WakeScreen(15);
    night_wake_until_us_ = 0;

    // QR app waiting for this amount -> "Đã nhận"
    if (mode_ == Mode::kApp && app_id_ == kAppQr && tx.amount > 0 &&
        (qr_amount_ == 0 || (uint64_t)tx.amount == qr_amount_)) {
        BuildReceivedOverlay(tx);
        ShowOverlay(Overlay::kReceived);
        overlay_until_us_ = esp_timer_get_time() + 8 * 1000000LL;
        return;
    }
    BuildTxOverlay(tx, false);
    ShowOverlay(Overlay::kTx);
    overlay_until_us_ = esp_timer_get_time() + (int64_t)kTxPopupSeconds * 1000000;
}

// ---------------------------------------------------------------- overlays

void Launcher::ShowOverlay(Overlay kind) {
    if (mode_ != Mode::kOverlay) overlay_return_ = mode_ == Mode::kHidden ? Mode::kHome : mode_;
    overlay_ = kind;
    overlay_until_us_ = 0;
    switch (kind) {
        case Overlay::kRing: BuildRingOverlay(); break;
        case Overlay::kNight: BuildNightOverlay(); break;
        case Overlay::kKeypad: BuildKeypadOverlay(); break;
        default: break;  // content already built by the caller
    }
    Show(Mode::kOverlay);
}

void Launcher::CloseOverlay() {
    Overlay was = overlay_;
    overlay_ = Overlay::kNone;
    lv_obj_add_flag(overlay_panel_, LV_OBJ_FLAG_HIDDEN);
    if (was == Overlay::kTx || was == Overlay::kTxDetail) BankStore::GetInstance().MarkRead();
    Mode back = overlay_return_;
    if (back == Mode::kOverlay || back == Mode::kHidden) back = Mode::kHome;
    if (back == Mode::kApp && app_id_ >= 0) {
        Show(Mode::kApp);
        OpenApp(app_id_);
    } else {
        Show(back);
    }
}

lv_obj_t* Launcher::OverlayRoot() {
    if (overlay_root_) lv_obj_delete_async(overlay_root_);
    lv_obj_t* p = lv_obj_create(overlay_panel_);
    overlay_root_ = p;
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, W, H);
    lv_obj_set_style_bg_color(p, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    return p;
}

void Launcher::BuildTxOverlay(const BankTx& t, bool detail) {
    lv_obj_t* p = OverlayRoot();
    bool in = t.amount >= 0;
    lv_obj_t* title = Label(p, nullptr, kText, detail ? "Chi tiết giao dịch" : "Giao dịch mới nhất");
    lv_obj_set_pos(title, 12, 4);
    Button(p, W - 76, 4, 66, 28, "Đóng", kCard2, kText, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->CloseOverlay();
    });
    lv_obj_t* amt = Label(p, f28_, in ? kGreen : kRed, Vnd(t.amount, true).c_str());
    lv_obj_set_pos(amt, 12, 34);
    lv_obj_t* chip = Chip(p, t.bank.c_str(), BankColor(t.bank), 0xFFFFFF, f14_);
    lv_obj_set_pos(chip, 12, 70);

    std::vector<std::pair<const char*, std::string>> rows;
    if (in && !t.name.empty()) rows.push_back({"Người gửi", t.name});
    if (in && !t.content.empty()) rows.push_back({"Nội dung", t.content});
    if (!t.account.empty()) rows.push_back({"Tài khoản", "*" + t.account});
    if (t.balance >= 0) rows.push_back({"Số dư sau", Vnd(t.balance, false)});
    rows.push_back({"Thời gian", WhenText(t.ts)});
    rows.push_back({"Nhận qua", t.source.empty() ? "LAN" : t.source});
    int y = 96;
    for (const auto& r : rows) {
        if (y > 206) break;
        lv_obj_t* k = Label(p, nullptr, kMuted, r.first);
        lv_obj_set_pos(k, 12, y);
        lv_obj_t* v = Label(p, nullptr, kText, r.second.c_str());
        lv_obj_set_width(v, W - 126);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(v, 116, y);
        y += 24;
    }
    if (!in && y <= 190) {
        lv_obj_t* n = Label(p, nullptr, kMuted, "Tiền ra không hiện người nhận và nội dung.");
        lv_obj_set_width(n, W - 24);
        lv_obj_set_pos(n, 12, y + 2);
    }
}

void Launcher::BuildReceivedOverlay(const BankTx& t) {
    lv_obj_t* p = OverlayRoot();
    lv_obj_set_style_bg_color(p, lv_color_hex(0x14532D), 0);
    lv_obj_t* circle = lv_obj_create(p);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 76, 76);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(kGreen), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_t* ok = Label(circle, f28_, 0x052E16, LV_SYMBOL_OK);
    lv_obj_center(ok);
    lv_obj_t* l = Label(p, f28_, 0xFFFFFF, ("Đã nhận " + Vnd(t.amount, false)).c_str());
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 120);
    std::string sub = t.bank + (t.name.empty() ? "" : " · " + t.name);
    lv_obj_t* s = Label(p, nullptr, 0xBBF7D0, sub.c_str());
    lv_obj_set_width(s, W - 24);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_add_event_cb(p, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        if (self->overlay_ == Overlay::kReceived) self->CloseOverlay();
    }, LV_EVENT_CLICKED, this);
}

void Launcher::BuildRingOverlay() {
    lv_obj_t* p = OverlayRoot();
    lv_obj_set_style_bg_color(p, lv_color_hex(kBg), 0);
    auto& am = AlarmManager::GetInstance();
    lv_obj_t* circle = lv_obj_create(p);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 60, 60);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t* ic = Label(circle, &font_material_symbols_30_4, 0x1F1300, MATERIAL_SYMBOLS_ALARM);
    lv_obj_center(ic);
    struct tm tmv = Now();
    lv_obj_t* t = Label(p, &lv_font_montserrat_48, kText, "");
    lv_label_set_text_fmt(t, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 76);
    std::string title = am.ring_title();
    std::string label = am.ring_label();
    lv_obj_t* l1 = Label(p, nullptr, kText, title.c_str());
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 132);
    lv_obj_t* l2 = Label(p, nullptr, kMuted, label.c_str());
    lv_obj_set_width(l2, W - 24);
    lv_obj_set_style_text_align(l2, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l2, LV_LABEL_LONG_DOT);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 156);
    Button(p, 12, 188, 140, 42, "Báo lại 5 phút", kCard2, kText, [](lv_event_t*) {
        AlarmManager::GetInstance().Snooze(5);
    });
    Button(p, 168, 188, 140, 42, "Tắt", 0xF59E0B, 0x1F1300, [](lv_event_t*) {
        AlarmManager::GetInstance().StopRinging();
    });
}

void Launcher::BuildNightOverlay() {
    lv_obj_t* p = OverlayRoot();
    lv_obj_set_style_bg_color(p, lv_color_black(), 0);
    struct tm tmv = Now();
    lv_obj_t* t = Label(p, &lv_font_montserrat_48, kAmber, "");
    lv_label_set_text_fmt(t, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_t* d = Label(p, nullptr, 0x6B5636, "");
    lv_label_set_text_fmt(d, "%s, %02d/%02d", kWeekdayLong[tmv.tm_wday % 7], tmv.tm_mday, tmv.tm_mon + 1);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 100);

    int y = 126;
    auto alarms = AlarmManager::GetInstance().ListAlarms();
    for (const auto& a : alarms) {
        if (!a.enabled) continue;
        char buf[64];
        snprintf(buf, sizeof(buf), "Báo thức %02d:%02d%s", a.hour, a.minute, a.repeat_daily ? " · hằng ngày" : "");
        lv_obj_t* l = Label(p, nullptr, 0x5C4A2E, buf);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
        y += 24;
        break;
    }
    BankTx tx;
    if (BankStore::GetInstance().Latest(tx) && time(nullptr) - tx.ts < 12 * 3600) {
        std::string s = Vnd(tx.amount, true) + " · " + tx.bank + " · " + Hm(tx.ts);
        lv_obj_t* l = Label(p, nullptr, 0x5C4A2E, s.c_str());
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    }
    lv_obj_t* hint = Label(p, nullptr, 0x4A3B25, "Chạm để sáng 10 giây");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(p, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        if (self->overlay_ != Overlay::kNight) return;
        AlarmManager::GetInstance().WakeScreen(10);
        self->night_wake_until_us_ = esp_timer_get_time() + 10 * 1000000LL;
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(p, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        if (self->overlay_ != Overlay::kNight) return;
        if (lv_indev_get_gesture_dir(lv_indev_active()) != LV_DIR_TOP) return;
        AlarmManager::GetInstance().WakeScreen(60);
        self->night_wake_until_us_ = 0;
        self->overlay_return_ = Mode::kMenu;
        self->CloseOverlay();
        lv_indev_wait_release(lv_indev_active());
    }, LV_EVENT_GESTURE, this);
}

void Launcher::BuildKeypadOverlay() {
    lv_obj_t* p = OverlayRoot();
    lv_obj_set_style_bg_color(p, lv_color_hex(kBg), 0);
    keypad_value_ = qr_amount_;
    lv_obj_t* t = Label(p, nullptr, kMuted, "Số tiền khách cần trả");
    lv_obj_set_pos(t, 10, 6);
    keypad_lbl_ = Label(p, f28_, kText, "");
    lv_obj_align(keypad_lbl_, LV_ALIGN_TOP_RIGHT, -10, 0);
    lv_label_set_text(keypad_lbl_, Vnd((int64_t)keypad_value_, false).c_str());

    static const char* kKeys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "000", "0", LV_SYMBOL_BACKSPACE};
    auto on_key = [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        int k = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
        uint64_t v = self->keypad_value_;
        if (k == 11) v /= 10;
        else if (k == 9) v *= 1000;
        else if (k == 10) v *= 10;
        else v = v * 10 + (uint64_t)(k + 1);
        if (v > 999999999999ULL) return;
        self->keypad_value_ = v;
        lv_label_set_text(self->keypad_lbl_, Vnd((int64_t)v, false).c_str());
    };
    for (int i = 0; i < 12; i++) {
        int col = i % 3, row = i / 3;
        lv_obj_t* b = Button(p, 8 + col * 74, 40 + row * 50, 70, 46, "", kCard2, kText, on_key);
        lv_obj_t* l = lv_obj_get_child(b, 0);
        lv_obj_set_style_text_font(l, f28_, 0);
        lv_label_set_text(l, kKeys[i]);
        lv_obj_set_user_data(b, (void*)(intptr_t)i);
    }
    lv_obj_t* done = Button(p, 232, 40, 80, 96, "Xong", kGreen, 0x052E16, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        self->qr_amount_ = self->keypad_value_;
        self->CloseOverlay();
    });
    (void)done;
    Button(p, 232, 140, 80, 46, "Xóa", kCard2, 0xF87171, [](lv_event_t* e) {
        auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
        self->keypad_value_ = 0;
        lv_label_set_text(self->keypad_lbl_, Vnd(0, false).c_str());
    });
    Button(p, 232, 190, 80, 46, "Hủy", kCard2, kText, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->CloseOverlay();
    });
}

// ---------------------------------------------------------------- apps

void Launcher::OpenApp(int id) {
    if (id == kAppChat) {
        Show(Mode::kHidden);
        busy_until_us_ = esp_timer_get_time() + 4000000;
        Application::GetInstance().Schedule([]() { Application::GetInstance().ToggleChatState(); });
        return;
    }
    if (id == kAppNight) {
        AlarmManager::GetInstance().ForceNight(true);
        night_wake_until_us_ = 0;
        overlay_return_ = Mode::kHome;
        ShowOverlay(Overlay::kNight);
        return;
    }

    // New content root; the old one is deleted after this event (a button inside it may
    // be the object that triggered this call)
    if (app_) lv_obj_delete_async(app_);
    app_ = lv_obj_create(app_panel_);
    lv_obj_remove_style_all(app_);
    lv_obj_set_size(app_, W, H);
    lv_obj_clear_flag(app_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(app_, LV_OBJ_FLAG_CLICKABLE);
    app_id_ = id;
    app_time_ = nullptr;
    app_dynamic_ = nullptr;

    // The menu should come back on the page that holds this app
    if (id < kAppCount) lv_obj_set_tile_id(menu_tiles_, id / 8, 0, LV_ANIM_OFF);

    switch (id) {
        case kAppPhoto:
            StatusBar(app_, "Ảnh", &app_time_);
            AppComingSoon("Khung ảnh Google Photos sẽ có ở bản sau: chiếu album khi loa rảnh, kèm giờ và thời tiết.");
            break;
        case kAppMusic:
            AppMusic();
            break;
        case kAppHud:
            StatusBar(app_, "Waze HUD", &app_time_);
            AppComingSoon("Dẫn đường kiểu HUD (mũi tên rẽ, tốc độ, camera) sẽ có ở bản sau.");
            break;
        case kAppFm: AppFm(); break;
        case kAppTimer: AppTimer(); break;
        case kAppQr: AppQr(); break;
        case kAppHistory: AppHistory(); break;
        case kAppSystem: AppSystem(); break;
        case kAppSettings: AppSettings(); break;
        case kAppWifi: AppWifi(); break;
        default: break;
    }
    if (mode_ != Mode::kApp) Show(Mode::kApp);
}

void Launcher::RefreshApp() {
    switch (app_id_) {
        case kAppFm: {
            static bool shown_playing = false;
            bool playing = RadioPlayer::GetInstance().IsPlaying();
            if (playing != shown_playing) {
                shown_playing = playing;
                OpenApp(kAppFm);
            }
            break;
        }
        case kAppTimer: {
            int remain = AlarmManager::GetInstance().TimerRemaining();
            bool shown = app_dynamic_ != nullptr;
            if ((remain > 0) != shown) {
                OpenApp(kAppTimer);
            } else if (shown) {
                lv_label_set_text_fmt(app_dynamic_, "%02d:%02d", remain / 60, remain % 60);
            }
            break;
        }
        case kAppSystem:
            if (tick_ % 3 == 0) OpenApp(kAppSystem);
            break;
        case kAppSettings:
            // QR appears once the LAN server is up
            if (app_dynamic_ == nullptr && LanServer::GetInstance().running()) OpenApp(kAppSettings);
            break;
        default:
            break;
    }
}

void Launcher::AppComingSoon(const char* text) {
    lv_obj_t* badge = Chip(app_, "Sắp có", 0x334155, kGold);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_t* body = Label(app_, nullptr, kMuted, text);
    lv_obj_set_width(body, W - 40);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 100);
}

// Music stations from the public Radio Browser directory, by genre
struct MusicGenre {
    const char* label;
    const char* query;
    bool vietnam;
};
static const MusicGenre kGenres[] = {
    {"Nhạc Việt", "music", true}, {"Bolero", "bolero", false}, {"Nhạc trẻ", "vpop", false},
    {"Không lời", "instrumental", false}, {"Lo-fi", "lofi", false}, {"Jazz", "jazz", false},
};

void Launcher::AppMusic() {
    StatusBar(app_, "Nhạc · Đài trực tuyến", &app_time_);
    auto& radio = RadioPlayer::GetInstance();
    bool playing = radio.IsPlaying();
    lv_obj_t* now = Card(app_, 8, 36, W - 16, 50, playing ? 0x3B0764 : kCard);
    lv_obj_t* name = Label(now, nullptr, playing ? kText : kMuted,
                           playing ? radio.CurrentName().c_str() : "Chọn thể loại hoặc nói \"mở nhạc bolero\"");
    lv_obj_set_width(name, 210);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
    if (playing) {
        lv_obj_t* stop = Button(now, 0, 0, 44, 36, LV_SYMBOL_STOP, 0xDB2777, 0xFFFFFF, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            self->RunAsync([]() { RadioPlayer::GetInstance().Stop(); });
        });
        lv_obj_set_style_radius(stop, 18, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(stop, 0), f14_, 0);
        lv_obj_align(stop, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    int n = sizeof(kGenres) / sizeof(kGenres[0]);
    for (int i = 0; i < n; i++) {
        int col = i % 3, row = i / 3;
        lv_obj_t* b = Button(app_, 8 + col * 104, 94 + row * 48, 98, 42, kGenres[i].label, kCard2, kText,
                             [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
            self->RunAsync([idx]() {
                auto& r = RadioPlayer::GetInstance();
                std::vector<RadioPlayer::FoundStation> found;
                std::string err;
                if (!r.SearchStations(kGenres[idx].query, kGenres[idx].vietnam, found, err) || found.empty()) {
                    r.SearchStations(kGenres[idx].query, !kGenres[idx].vietnam, found, err);
                }
                if (!found.empty()) {
                    r.PlayUrl(found[0].url, found[0].name, found[0].codec);
                } else {
                    Application::GetInstance().Schedule([]() {
                        auto display = Board::GetInstance().GetDisplay();
                        if (display) display->ShowNotification("Không tìm được đài", 3000);
                    });
                }
            });
        });
        lv_obj_set_user_data(b, (void*)(intptr_t)i);
    }
    lv_obj_t* note = Label(app_, nullptr, kMuted, "Nguồn: danh bạ Radio Browser");
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void Launcher::AppFm() {
    StatusBar(app_, "FM · Trực tiếp", &app_time_);
    auto& radio = RadioPlayer::GetInstance();
    bool playing = radio.IsPlaying();
    lv_obj_t* now = Card(app_, 8, 36, W - 16, 50, playing ? 0x3B0764 : kCard);
    lv_obj_t* name = Label(now, nullptr, playing ? kText : kMuted, playing ? radio.CurrentName().c_str() : "Chưa phát");
    lv_obj_set_width(name, 210);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
    if (playing) {
        lv_obj_t* stop = Button(now, 0, 0, 44, 36, LV_SYMBOL_STOP, 0xDB2777, 0xFFFFFF, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            self->RunAsync([]() { RadioPlayer::GetInstance().Stop(); });
        });
        lv_obj_set_style_radius(stop, 18, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(stop, 0), f14_, 0);
        lv_obj_align(stop, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    int n = RadioPlayer::StationCount();
    for (int i = 0; i < n && i < 9; i++) {
        int col = i % 3, row = i / 3;
        bool active = playing && radio.CurrentName() == RadioPlayer::StationName(i);
        lv_obj_t* b = Button(app_, 8 + col * 104, 94 + row * 48, 98, 42, RadioPlayer::StationKey(i),
                             active ? 0x7C3AED : kCard2, kText, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
            std::string key = RadioPlayer::StationKey(idx);
            self->RunAsync([key]() { RadioPlayer::GetInstance().Play(key); });
        });
        if (active) {
            lv_obj_set_style_border_color(b, lv_color_hex(0xE879F9), 0);
            lv_obj_set_style_border_width(b, 2, 0);
        }
        lv_obj_set_user_data(b, (void*)(intptr_t)i);
    }
}

void Launcher::AppTimer() {
    StatusBar(app_, "Hẹn giờ", &app_time_);
    auto& am = AlarmManager::GetInstance();
    int remain = am.TimerRemaining();

    lv_obj_t* box = Card(app_, 8, 36, W - 16, 48);
    if (remain > 0) {
        lv_obj_t* l = Label(box, nullptr, kMuted, "Còn");
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
        app_dynamic_ = Label(box, f28_, 0xFB923C, "");
        lv_label_set_text_fmt(app_dynamic_, "%02d:%02d", remain / 60, remain % 60);
        lv_obj_align(app_dynamic_, LV_ALIGN_LEFT_MID, 56, 0);
        lv_obj_t* c = Button(box, 0, 0, 70, 34, "Hủy", 0x7F1D1D, 0xFFFFFF, [](lv_event_t* e) {
            AlarmManager::GetInstance().CancelTimer();
            static_cast<Launcher*>(lv_event_get_user_data(e))->OpenApp(kAppTimer);
        });
        lv_obj_align(c, LV_ALIGN_RIGHT_MID, -6, 0);
    } else {
        lv_obj_t* l = Label(box, nullptr, kMuted, "Đếm ngược");
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
        static const int kPresets[] = {1, 5, 10, 30};
        for (int i = 0; i < 4; i++) {
            char txt[16];
            snprintf(txt, sizeof(txt), "%d'", kPresets[i]);
            lv_obj_t* b = Button(box, 112 + i * 49, 6, 45, 36, txt, kCard2, kText, [](lv_event_t* e) {
                auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
                int mins = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
                AlarmManager::GetInstance().StartTimer(mins * 60, "Hẹn giờ " + std::to_string(mins) + " phút");
                self->OpenApp(kAppTimer);
            });
            lv_obj_set_user_data(b, (void*)(intptr_t)kPresets[i]);
        }
    }

    auto alarms = am.ListAlarms();
    if (alarms.empty()) {
        lv_obj_t* l = Label(app_, nullptr, kMuted, "Chưa có báo thức.\nNói \"đặt báo thức 6 giờ 30\" để thêm.");
        lv_obj_set_width(l, W - 24);
        lv_obj_set_pos(l, 12, 100);
        return;
    }
    lv_obj_t* list = lv_obj_create(app_);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, W, H - 90);
    lv_obj_set_pos(list, 0, 90);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_hor(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    for (size_t i = 0; i < alarms.size(); i++) {
        const auto& a = alarms[i];
        lv_obj_t* row = Card(list, 0, 0, W - 16, 44);
        lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t* t = Label(row, f28_, a.enabled ? kText : 0x64748B, "");
        lv_label_set_text_fmt(t, "%02d:%02d", a.hour, a.minute);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
        std::string desc = std::string(a.repeat_daily ? "Hằng ngày" : "Một lần") + (a.label.empty() ? "" : " · " + a.label);
        lv_obj_t* d = Label(row, nullptr, kMuted, desc.c_str());
        lv_obj_set_width(d, 124);
        lv_label_set_long_mode(d, LV_LABEL_LONG_DOT);
        lv_obj_align(d, LV_ALIGN_LEFT_MID, 100, 0);
        lv_obj_t* b = Button(row, 0, 0, 62, 30, a.enabled ? "Bật" : "Tắt", a.enabled ? 0xEA580C : 0x334155,
                             0xFFFFFF, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
            auto list = AlarmManager::GetInstance().ListAlarms();
            if (idx < (int)list.size()) AlarmManager::GetInstance().SetAlarmEnabled(idx, !list[idx].enabled);
            self->OpenApp(kAppTimer);
        });
        lv_obj_set_user_data(b, (void*)(intptr_t)i);
        lv_obj_align(b, LV_ALIGN_RIGHT_MID, -6, 0);
    }
}

void Launcher::AppQr() {
    auto accounts = BankStore::GetInstance().Accounts();
    lv_obj_t* title = Label(app_, nullptr, kText, "QR");
    lv_obj_set_pos(title, 12, 3);
    if (accounts.empty()) {
        lv_obj_t* l = Label(app_, nullptr, kMuted,
                            "Chưa có tài khoản nhận tiền.\nMở Menu → Cài đặt, quét mã QR bằng điện thoại rồi thêm tối đa 3 tài khoản.");
        lv_obj_set_width(l, W - 24);
        lv_obj_set_pos(l, 12, 40);
        return;
    }
    if (app_page_ >= (int)accounts.size()) app_page_ = 0;
    const QrAccount& a = accounts[app_page_];

    std::string dots;
    for (size_t i = 0; i < accounts.size(); i++) dots += (int)i == app_page_ ? LV_SYMBOL_BULLET : "·";
    lv_obj_t* tag = Label(app_, f14_, kMuted, ("VIETQR  " + dots).c_str());
    lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -12, 8);

    lv_obj_t* white = Card(app_, 8, 32, 172, 172, 0xFFFFFF);
    lv_obj_add_flag(white, LV_OBJ_FLAG_GESTURE_BUBBLE);
    std::string payload = vietqr::BuildPayload(a.bin, a.number, qr_amount_, "");
    lv_obj_t* q = QrCanvas(white, payload, 164, lv_color_black(), lv_color_white());
    lv_obj_center(q);

    std::string bank = vietqr::BankName(a.bin);
    lv_obj_t* chip = Chip(app_, bank.c_str(), BankColor(bank), 0xFFFFFF, f14_);
    lv_obj_set_pos(chip, 188, 34);
    lv_obj_t* k1 = Label(app_, nullptr, kMuted, "Số tài khoản");
    lv_obj_set_pos(k1, 188, 56);
    lv_obj_t* num = Label(app_, f14_, kText, GroupDigits(a.number).c_str());
    lv_obj_set_width(num, 128);
    lv_label_set_long_mode(num, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(num, 188, 80);
    lv_obj_t* holder = Label(app_, f14_, kText, a.holder.c_str());
    lv_obj_set_width(holder, 128);
    lv_label_set_long_mode(holder, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(holder, 188, 98);
    lv_obj_t* k2 = Label(app_, nullptr, kMuted, "Số tiền");
    lv_obj_set_pos(k2, 188, 116);
    lv_obj_t* amt = Label(app_, qr_amount_ ? f28_ : nullptr, qr_amount_ ? kGreen : kMuted,
                          qr_amount_ ? Vnd((int64_t)qr_amount_, false).c_str() : "Khách tự nhập");
    lv_obj_set_width(amt, 128);
    lv_label_set_long_mode(amt, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(amt, 188, 140);
    Button(app_, 188, 176, 124, 34, "Đổi số tiền", 0x3B82F6, 0xFFFFFF, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->ShowOverlay(Overlay::kKeypad);
    });
    lv_obj_t* hint = Label(app_, nullptr, kMuted,
                           accounts.size() > 1 ? "Vuốt trái/phải để đổi tài khoản" : "Khách chuyển xong loa sẽ báo");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void Launcher::AppHistory() {
    lv_obj_t* title = Label(app_, nullptr, kText, "Lịch sử");
    lv_obj_set_pos(title, 10, 3);
    struct tm tmv = Now();
    int prev_month = tmv.tm_mon == 0 ? 12 : tmv.tm_mon;
    char t1[24], t2[24];
    snprintf(t1, sizeof(t1), "Tháng %d", tmv.tm_mon + 1);
    snprintf(t2, sizeof(t2), "Tháng %d", prev_month);
    const char* tabs[3] = {"Hôm nay", t1, t2};
    int x = W - 6;
    for (int i = 2; i >= 0; i--) {
        lv_obj_t* b = lv_button_create(app_);
        lv_obj_set_height(b, 26);
        lv_obj_set_width(b, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(b, 6, 0);
        lv_obj_set_style_pad_ver(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(i == app_page_ ? 0x334155 : kBg), 0);
        lv_obj_t* l = Label(b, f14_, i == app_page_ ? kText : kMuted, tabs[i]);
        lv_obj_center(l);
        lv_obj_update_layout(b);
        x -= lv_obj_get_width(b);
        lv_obj_set_pos(b, x, 3);
        x -= 2;
        lv_obj_set_user_data(b, (void*)(intptr_t)i);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            self->app_page_ = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
            self->OpenApp(kAppHistory);
        }, LV_EVENT_CLICKED, this);
    }

    int64_t from, to;
    if (app_page_ == 0) {
        from = DayStart(time(nullptr));
        to = from + 86400 * 2;
    } else if (app_page_ == 1) {
        from = MonthStart(0);
        to = MonthStart(-1);
    } else {
        from = MonthStart(1);
        to = MonthStart(0);
    }
    int64_t in_sum, out_sum;
    int count;
    auto& store = BankStore::GetInstance();
    store.Totals(from, to, in_sum, out_sum, count);
    lv_obj_t* s_in = Label(app_, f14_, kGreen, (LV_SYMBOL_DOWN " " + Vnd(in_sum, false)).c_str());
    lv_obj_set_pos(s_in, 10, 34);
    lv_obj_t* s_out = Label(app_, f14_, 0xF87171, (LV_SYMBOL_UP " " + Vnd(out_sum, false)).c_str());
    lv_obj_set_pos(s_out, 130, 34);
    lv_obj_t* s_n = Label(app_, f14_, kMuted, (std::to_string(count) + " giao dịch").c_str());
    lv_obj_align(s_n, LV_ALIGN_TOP_RIGHT, -10, 34);

    lv_obj_t* list = lv_obj_create(app_);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, W, H - 56);
    lv_obj_set_pos(list, 0, 56);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);

    list_cache_.clear();
    int64_t last_day = -1;
    int shown = 0;
    for (const auto& t : store.List()) {
        if (t.ts < from || t.ts >= to) continue;
        if (shown >= 40) break;
        int64_t day = DayStart(t.ts);
        if (app_page_ != 0 && day != last_day) {
            last_day = day;
            int64_t din, dout;
            int dn;
            store.Totals(day, day + 86400, din, dout, dn);
            time_t dt = (time_t)day;
            struct tm dtm;
            localtime_r(&dt, &dtm);
            lv_obj_t* hdr = Card(list, 0, 0, W, 24, 0x1E3A5F);
            lv_obj_set_style_radius(hdr, 0, 0);
            lv_obj_t* hl = Label(hdr, f14_, kText, "");
            lv_label_set_text_fmt(hl, "%02d/%02d", dtm.tm_mday, dtm.tm_mon + 1);
            lv_obj_align(hl, LV_ALIGN_LEFT_MID, 10, 0);
            lv_obj_t* hr = Label(hdr, f14_, kMuted, ("+" + FormatThousands(din) + "  -" + FormatThousands(dout)).c_str());
            lv_obj_align(hr, LV_ALIGN_RIGHT_MID, -10, 0);
        }
        lv_obj_t* row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, W, 46);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(kCard2), 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* tm_l = Label(row, f14_, kMuted, Hm(t.ts).c_str());
        lv_obj_align(tm_l, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t* chip = Chip(row, t.bank.c_str(), BankColor(t.bank), 0xFFFFFF, f14_);
        lv_obj_set_pos(chip, 54, 3);
        std::string who = t.amount >= 0 ? (t.name.empty() ? t.content : t.name) : "Chuyển đi";
        lv_obj_t* w = Label(row, f14_, kMuted, who.c_str());
        lv_obj_set_width(w, 150);
        lv_label_set_long_mode(w, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(w, 56, 25);
        lv_obj_t* a = Label(row, f14_, t.amount >= 0 ? kGreen : 0xF87171,
                            ((t.amount >= 0 ? "+" : "-") + FormatThousands(t.amount)).c_str());
        lv_obj_align(a, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_user_data(row, (void*)(intptr_t)list_cache_.size());
        list_cache_.push_back(t);
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<Launcher*>(lv_event_get_user_data(e));
            size_t idx = (size_t)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
            if (idx >= self->list_cache_.size()) return;
            self->BuildTxOverlay(self->list_cache_[idx], true);
            self->ShowOverlay(Overlay::kTxDetail);
        }, LV_EVENT_CLICKED, this);
        shown++;
    }
    if (shown == 0) {
        lv_obj_t* l = Label(list, nullptr, kMuted, "Chưa có giao dịch trong khoảng này.");
        lv_obj_set_style_pad_all(l, 12, 0);
    } else {
        lv_obj_t* hint = Label(list, nullptr, kMuted, "Tải CSV (Excel) ở trang /setup");
        lv_obj_set_style_pad_all(hint, 10, 0);
    }
}

void Launcher::AppSystem() {
    StatusBar(app_, "Hệ thống", &app_time_);
    auto& wifi = WifiManager::GetInstance();
    int64_t up = esp_timer_get_time() / 1000000;
    size_t sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t sram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    float temp = 0;
    bool has_temp = Board::GetInstance().GetTemperature(temp);
    char b[8][48];
    snprintf(b[0], 48, "%s", wifi.IsConnected() ? wifi.GetSsid().c_str() : "Chưa kết nối");
    snprintf(b[1], 48, "%s", wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "-");
    snprintf(b[2], 48, "%d dBm", wifi.IsConnected() ? wifi.GetRssi() : 0);
    snprintf(b[3], 48, "%uK (min %uK)", (unsigned)(sram / 1024), (unsigned)(sram_min / 1024));
    snprintf(b[4], 48, "%.1f MB trống", psram / 1048576.0);
    if (has_temp) snprintf(b[5], 48, "%.1f°C", temp);
    else snprintf(b[5], 48, "-");
    snprintf(b[6], 48, "%d giờ %d phút", (int)(up / 3600), (int)(up % 3600 / 60));
    snprintf(b[7], 48, "KenDev %s", esp_app_get_description()->version);
    static const char* kKeys[8] = {"Wi-Fi", "Địa chỉ IP", "Tín hiệu", "RAM", "PSRAM", "Nhiệt độ chip",
                                   "Thời gian chạy", "Phiên bản"};
    for (int i = 0; i < 8; i++) {
        int col = i % 2, row = i / 2;
        lv_obj_t* c = Card(app_, 6 + col * 156, 34 + row * 51, 152, 47);
        lv_obj_add_flag(c, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t* k = Label(c, nullptr, kMuted, kKeys[i]);
        lv_obj_set_pos(k, 8, 1);
        lv_obj_t* v = Label(c, nullptr, kText, b[i]);
        lv_obj_set_width(v, 140);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(v, 8, 23);
    }
}

void Launcher::AppSettings() {
    StatusBar(app_, "Cài đặt", &app_time_);
    auto& lan = LanServer::GetInstance();
    std::string url = lan.SetupUrl();
    if (url.empty()) {
        lv_obj_t* l = Label(app_, nullptr, kMuted, "Loa chưa vào mạng Wi-Fi nên chưa mở được trang cài đặt.");
        lv_obj_set_width(l, W - 24);
        lv_obj_set_pos(l, 12, 40);
    } else {
        lv_obj_t* white = Card(app_, 8, 36, 150, 150, 0xFFFFFF);
        lv_obj_add_flag(white, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t* q = QrCanvas(white, url, 144, lv_color_black(), lv_color_white());
        lv_obj_center(q);
        app_dynamic_ = q;
        auto& wifi = WifiManager::GetInstance();
        auto token = BankStore::GetInstance().Settings().token;
        std::string steps = "1. Vào Wi-Fi\n" + wifi.GetSsid() + "\n2. Quét QR / mở\n" + wifi.GetIpAddress() +
                            "/setup\n3. Mã: " + token;
        lv_obj_t* s = Label(app_, nullptr, kText, steps.c_str());
        lv_obj_set_width(s, 148);
        lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(s, 166, 38);
    }
    Button(app_, 8, 194, 150, 40, "Cân chỉnh cảm ứng", kCard2, kText, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->StartCalibration();
    });
    Button(app_, 166, 194, 146, 40, "Wi-Fi", kCard2, kText, [](lv_event_t* e) {
        static_cast<Launcher*>(lv_event_get_user_data(e))->OpenApp(kAppWifi);
    });
}

void Launcher::AppWifi() {
    StatusBar(app_, "Wi-Fi", &app_time_);
    auto& wifi = WifiManager::GetInstance();
    std::string info = wifi.IsConnected()
                           ? "Đang dùng: " + wifi.GetSsid() + "\nIP " + wifi.GetIpAddress() + " · " +
                                 std::to_string(wifi.GetRssi()) + " dBm"
                           : std::string("Chưa kết nối Wi-Fi.");
    lv_obj_t* l = Label(app_, nullptr, kText, info.c_str());
    lv_obj_set_width(l, W - 24);
    lv_obj_set_pos(l, 12, 40);
    lv_obj_t* h = Label(app_, nullptr, kMuted,
                        "Đổi mạng: loa sẽ phát Wi-Fi KenDev-xxxx. Dùng điện thoại vào mạng đó để chọn Wi-Fi mới.");
    lv_obj_set_width(h, W - 24);
    lv_obj_set_pos(h, 12, 96);
    Button(app_, 12, 186, W - 24, 44, "Cài Wi-Fi mới", 0x2563EB, 0xFFFFFF, [](lv_event_t*) {
        auto& board = static_cast<WifiBoard&>(Board::GetInstance());
        board.EnterWifiConfigMode();
    });
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
    // The mapping is stored for the normal orientation. With the screen rotated by 180°,
    // the first target sits at the panel's bottom-right corner, so swap the two points.
    bool flipped = BankStore::GetInstance().Settings().rotate180;
    int a = flipped ? 1 : 0, b = flipped ? 0 : 1;
    bool ok = touch_cal::Solve(calib_raw_[a][0], calib_raw_[a][1], calib_raw_[b][0], calib_raw_[b][1], W, H);
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
