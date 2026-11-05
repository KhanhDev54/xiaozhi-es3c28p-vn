#include "alarm_manager.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ctime>
#include <cstdio>

#include "application.h"
#include "assets/lang_config.h"
#include "backlight.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "settings.h"
#include "lunar_calendar.h"

#define TAG "AlarmManager"

static const int kMaxAlarms = 8;
static const int kRingMaxSeconds = 60;
static const int kRingEverySeconds = 3;
static const int kNightBrightness = 5;

// ---------------------------------------------------------------- storage

void AlarmManager::Load() {
    Settings s("alarm", false);
    std::string raw = s.GetString("list", "[]");
    night_enabled_ = s.GetInt("night_on", 1) != 0;
    night_start_ = s.GetInt("night_start", 22);
    night_end_ = s.GetInt("night_end", 6);

    alarms_.clear();
    cJSON* arr = cJSON_Parse(raw.c_str());
    if (cJSON_IsArray(arr)) {
        cJSON* it = nullptr;
        cJSON_ArrayForEach(it, arr) {
            AlarmItem a;
            cJSON* h = cJSON_GetObjectItem(it, "h");
            cJSON* m = cJSON_GetObjectItem(it, "m");
            if (!cJSON_IsNumber(h) || !cJSON_IsNumber(m)) continue;
            a.hour = h->valueint;
            a.minute = m->valueint;
            a.repeat_daily = cJSON_IsTrue(cJSON_GetObjectItem(it, "r"));
            cJSON* e = cJSON_GetObjectItem(it, "e");
            a.enabled = !cJSON_IsBool(e) || cJSON_IsTrue(e);
            cJSON* l = cJSON_GetObjectItem(it, "l");
            if (cJSON_IsString(l)) a.label = l->valuestring;
            alarms_.push_back(a);
        }
    }
    cJSON_Delete(arr);
}

void AlarmManager::Save() {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& a : alarms_) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "h", a.hour);
        cJSON_AddNumberToObject(o, "m", a.minute);
        cJSON_AddBoolToObject(o, "r", a.repeat_daily);
        cJSON_AddBoolToObject(o, "e", a.enabled);
        cJSON_AddStringToObject(o, "l", a.label.c_str());
        cJSON_AddItemToArray(arr, o);
    }
    char* txt = cJSON_PrintUnformatted(arr);
    Settings s("alarm", true);
    s.SetString("list", txt ? txt : "[]");
    s.SetInt("night_on", night_enabled_ ? 1 : 0);
    s.SetInt("night_start", night_start_);
    s.SetInt("night_end", night_end_);
    cJSON_free(txt);
    cJSON_Delete(arr);
}

// ---------------------------------------------------------------- API

bool AlarmManager::AddAlarm(const AlarmItem& item, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (item.hour < 0 || item.hour > 23 || item.minute < 0 || item.minute > 59) {
        error = "Giờ không hợp lệ";
        return false;
    }
    if ((int)alarms_.size() >= kMaxAlarms) {
        error = "Đã đủ 8 báo thức, hãy xoá bớt";
        return false;
    }
    alarms_.push_back(item);
    Save();
    return true;
}

std::vector<AlarmItem> AlarmManager::ListAlarms() {
    std::lock_guard<std::mutex> lock(mutex_);
    return alarms_;
}

bool AlarmManager::DeleteAlarm(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0) {
        alarms_.clear();
    } else if (index < (int)alarms_.size()) {
        alarms_.erase(alarms_.begin() + index);
    } else {
        return false;
    }
    Save();
    return true;
}

void AlarmManager::StartTimer(int seconds, const std::string& label) {
    std::lock_guard<std::mutex> lock(mutex_);
    timer_end_us_ = esp_timer_get_time() + (int64_t)seconds * 1000000;
    timer_label_ = label;
}

void AlarmManager::CancelTimer() {
    std::lock_guard<std::mutex> lock(mutex_);
    timer_end_us_ = 0;
}

int AlarmManager::TimerRemaining() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (timer_end_us_ == 0) return 0;
    int64_t left = timer_end_us_ - esp_timer_get_time();
    return left > 0 ? (int)((left + 999999) / 1000000) : 0;
}

void AlarmManager::SetNightMode(bool enabled, int start_hour, int end_hour) {
    std::lock_guard<std::mutex> lock(mutex_);
    night_enabled_ = enabled;
    night_start_ = start_hour % 24;
    night_end_ = end_hour % 24;
    Save();
}

void AlarmManager::WakeScreen(int seconds) {
    wake_until_us_ = esp_timer_get_time() + (int64_t)seconds * 1000000;
    if (night_active_) {
        if (auto* bl = Board::GetInstance().GetBacklight()) bl->RestoreBrightness();
        night_active_ = false;
    }
}

void AlarmManager::StopRinging() {
    if (!ringing_) return;
    ringing_ = false;
    ring_seconds_ = 0;
    Application::GetInstance().Schedule([]() { Application::GetInstance().DismissAlert(); });
}

// ---------------------------------------------------------------- runtime

void AlarmManager::Ring(const std::string& title, const std::string& label) {
    ESP_LOGI(TAG, "Ring: %s %s", title.c_str(), label.c_str());
    ringing_ = true;
    ring_seconds_ = 0;
    // Wake the screen up
    if (auto* bl = Board::GetInstance().GetBacklight()) bl->RestoreBrightness();
    night_active_ = false;
    std::string t = title, l = label;
    Application::GetInstance().Schedule([t, l]() {
        Application::GetInstance().Alert(t.c_str(), l.c_str(), "surprised", Lang::Sounds::OGG_VIBRATION);
    });
}

void AlarmManager::UpdateNightMode(int hour) {
    if (!night_enabled_ && !night_active_) return;
    auto* bl = Board::GetInstance().GetBacklight();
    if (!bl) return;
    auto& app = Application::GetInstance();
    bool in_range;
    if (night_start_ == night_end_) {
        in_range = false;
    } else if (night_start_ < night_end_) {
        in_range = hour >= night_start_ && hour < night_end_;
    } else {
        in_range = hour >= night_start_ || hour < night_end_;
    }
    bool want = night_enabled_ && in_range && !ringing_ &&
                esp_timer_get_time() >= wake_until_us_ &&
                app.GetDeviceState() == kDeviceStateIdle;
    if (want && !night_active_) {
        ESP_LOGI(TAG, "Night mode on");
        bl->SetBrightness(kNightBrightness, false);
        night_active_ = true;
    } else if (!want && night_active_) {
        ESP_LOGI(TAG, "Night mode off");
        bl->RestoreBrightness();
        night_active_ = false;
    }
}

void AlarmManager::Tick() {
    auto& app = Application::GetInstance();

    // Ringing: repeat sound, stop on user interaction or after 60 s
    if (ringing_) {
        ring_seconds_++;
        DeviceState st = app.GetDeviceState();
        if (st == kDeviceStateListening || st == kDeviceStateSpeaking || st == kDeviceStateConnecting) {
            StopRinging();
        } else if (ring_seconds_ >= kRingMaxSeconds) {
            StopRinging();
        } else if (ring_seconds_ % kRingEverySeconds == 0) {
            app.Schedule([]() { Application::GetInstance().PlaySound(Lang::Sounds::OGG_VIBRATION); });
        }
    }

    // Countdown timer
    std::string timer_label;
    bool timer_done = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (timer_end_us_ != 0 && esp_timer_get_time() >= timer_end_us_) {
            timer_end_us_ = 0;
            timer_done = true;
            timer_label = timer_label_;
        }
    }
    if (timer_done) {
        Ring("Hết giờ", timer_label.empty() ? "Hẹn giờ đã kết thúc" : timer_label);
    }

    // Wall-clock alarms (only when time is synced)
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (tmv.tm_year + 1900 < 2024) return;

    int minute_of_day = tmv.tm_hour * 60 + tmv.tm_min;
    if (minute_of_day != last_checked_minute_) {
        last_checked_minute_ = minute_of_day;
        AlarmItem fired;
        bool fire = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 0; i < alarms_.size(); i++) {
                auto& a = alarms_[i];
                if (a.enabled && a.hour == tmv.tm_hour && a.minute == tmv.tm_min) {
                    fired = a;
                    fire = true;
                    if (!a.repeat_daily) {
                        a.enabled = false;
                        Save();
                    }
                    break;
                }
            }
        }
        if (fire) {
            char title[64];
            snprintf(title, sizeof(title), "Báo thức %02d:%02d", fired.hour, fired.minute);
            Ring(title, fired.label.empty() ? "Đến giờ rồi!" : fired.label);
        }
    }

    UpdateNightMode(tmv.tm_hour);
}

void AlarmManager::TaskEntry(void* arg) {
    auto* self = static_cast<AlarmManager*>(arg);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        self->Tick();
    }
}

void AlarmManager::Start(bool has_backlight) {
    if (started_) return;
    started_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Load();
        if (!has_backlight) night_enabled_ = false;
    }
    RegisterTools(has_backlight);
    xTaskCreate(TaskEntry, "alarm_task", 4096, this, 3, nullptr);
}

// ---------------------------------------------------------------- MCP tools

static std::string Hhmm(int h, int m) {
    char b[32];
    snprintf(b, sizeof(b), "%02d:%02d", h, m);
    return b;
}

void AlarmManager::RegisterTools(bool has_backlight) {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.alarm.set",
        "Đặt báo thức theo giờ (giờ địa phương, 24h). Ví dụ '6 giờ 30 sáng mai' -> hour=6, minute=30;\n"
        "'mỗi ngày 7 giờ' -> repeat_daily=true. Báo thức kêu tối đa 1 phút, nói chuyện để tắt.\n"
        "Nếu người dùng nói 'hẹn giờ 10 phút' (đếm ngược) thì dùng self.timer.start thay vì tool này.",
        PropertyList({
            Property("hour", kPropertyTypeInteger, 0, 23),
            Property("minute", kPropertyTypeInteger, 0, 0, 59),
            Property("repeat_daily", kPropertyTypeBoolean, false),
            Property("label", kPropertyTypeString, ""),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            AlarmItem a;
            a.hour = p["hour"].value<int>();
            a.minute = p["minute"].value<int>();
            a.repeat_daily = p["repeat_daily"].value<bool>();
            a.label = p["label"].value<std::string>();
            std::string err;
            if (!AddAlarm(a, err)) {
                return "{\"success\": false, \"message\": \"" + err + "\"}";
            }
            return "{\"success\": true, \"message\": \"Đã đặt báo thức " + Hhmm(a.hour, a.minute) +
                   (a.repeat_daily ? " hằng ngày" : "") + "\"}";
        });

    mcp.AddTool("self.alarm.list",
        "Liệt kê các báo thức đã đặt (kèm số thứ tự index để xoá).",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            auto list = ListAlarms();
            cJSON* arr = cJSON_CreateArray();
            for (size_t i = 0; i < list.size(); i++) {
                cJSON* o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "index", (int)i);
                cJSON_AddStringToObject(o, "time", Hhmm(list[i].hour, list[i].minute).c_str());
                cJSON_AddBoolToObject(o, "repeat_daily", list[i].repeat_daily);
                cJSON_AddBoolToObject(o, "enabled", list[i].enabled);
                cJSON_AddStringToObject(o, "label", list[i].label.c_str());
                cJSON_AddItemToArray(arr, o);
            }
            int left = TimerRemaining();
            cJSON* root = cJSON_CreateObject();
            cJSON_AddItemToObject(root, "alarms", arr);
            cJSON_AddNumberToObject(root, "timer_seconds_left", left);
            return root;
        });

    mcp.AddTool("self.alarm.delete",
        "Xoá báo thức theo index (lấy từ self.alarm.list). index = -1 để xoá tất cả.",
        PropertyList({Property("index", kPropertyTypeInteger, -1, 99)}),
        [this](const PropertyList& p) -> ReturnValue {
            bool ok = DeleteAlarm(p["index"].value<int>());
            return ok ? "{\"success\": true}" : "{\"success\": false, \"message\": \"Không có báo thức này\"}";
        });

    mcp.AddTool("self.alarm.stop",
        "Tắt chuông báo thức / hẹn giờ đang kêu.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            StopRinging();
            return true;
        });

    mcp.AddTool("self.timer.start",
        "Hẹn giờ đếm ngược. Ví dụ 'hẹn giờ 10 phút' -> minutes=10; '90 giây' -> seconds=90.",
        PropertyList({
            Property("minutes", kPropertyTypeInteger, 0, 0, 1440),
            Property("seconds", kPropertyTypeInteger, 0, 0, 3600),
            Property("label", kPropertyTypeString, ""),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            int total = p["minutes"].value<int>() * 60 + p["seconds"].value<int>();
            if (total <= 0) {
                return "{\"success\": false, \"message\": \"Thời gian phải lớn hơn 0\"}";
            }
            StartTimer(total, p["label"].value<std::string>());
            return "{\"success\": true, \"seconds\": " + std::to_string(total) + "}";
        });

    mcp.AddTool("self.timer.cancel",
        "Huỷ hẹn giờ đếm ngược đang chạy.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            CancelTimer();
            return true;
        });

    mcp.AddTool("self.calendar.today",
        "Ngày hôm nay theo dương lịch và ÂM LỊCH Việt Nam: ngày/tháng/năm âm, can chi của năm,\n"
        "tháng, ngày và các giờ hoàng đạo. Dùng khi người dùng hỏi về âm lịch, ngày rằm, mùng 1, giờ tốt.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            time_t now = time(nullptr);
            struct tm t;
            localtime_r(&now, &t);
            if (t.tm_year + 1900 < 2024) {
                return "{\"success\": false, \"message\": \"Thiết bị chưa đồng bộ giờ\"}";
            }
            int d = t.tm_mday, m = t.tm_mon + 1, y = t.tm_year + 1900;
            auto l = lunar::SolarToLunar(d, m, y);
            char solar[40];
            snprintf(solar, sizeof(solar), "%02d/%02d/%04d", d, m, y);
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "solar_date", solar);
            cJSON_AddNumberToObject(o, "lunar_day", l.day);
            cJSON_AddNumberToObject(o, "lunar_month", l.month);
            cJSON_AddNumberToObject(o, "lunar_year", l.year);
            cJSON_AddBoolToObject(o, "leap_month", l.leap);
            cJSON_AddStringToObject(o, "year_can_chi", lunar::YearCanChi(l.year).c_str());
            cJSON_AddStringToObject(o, "month_can_chi", lunar::MonthCanChi(l.month, l.year).c_str());
            cJSON_AddStringToObject(o, "day_can_chi", lunar::DayCanChi(d, m, y).c_str());
            cJSON_AddStringToObject(o, "gio_hoang_dao", lunar::AuspiciousHours(d, m, y).c_str());
            return o;
        });

    if (!has_backlight) {
        return;  // OLED boards: no backlight to dim, skip night-mode tool
    }
    mcp.AddTool("self.night_mode.set",
        "Bật/tắt chế độ ban đêm: trong khung giờ này, khi rảnh màn hình tự giảm độ sáng còn 5%.\n"
        "Mặc định 22 giờ đến 6 giờ sáng.",
        PropertyList({
            Property("enabled", kPropertyTypeBoolean),
            Property("start_hour", kPropertyTypeInteger, 22, 0, 23),
            Property("end_hour", kPropertyTypeInteger, 6, 0, 23),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            SetNightMode(p["enabled"].value<bool>(), p["start_hour"].value<int>(), p["end_hour"].value<int>());
            return true;
        });
}
