#include <algorithm>
#include <cstdint>
#include <cctype>
#include <string>

#include <cJSON.h>
#include <esp_log.h>

#include "config.h"
#include "features/QRCode/qrcode_display.h"
#include "hunonic_controller.h"
#include "hunonic_gait.h"
#include "mcp_server.h"
#include "settings.h"
#include "wifi_station.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAG "HunonicCtrl"

enum class HunonicCmdType : uint8_t {
    Stop = 0,
    WalkForward,
    WalkBackward,
    WalkLeft,
    WalkRight,
    SlideForward,
    SlideBackward,
    SlideLeft,
    SlideRight,
    Dance,
    Patrol,
};

struct HunonicCmd {
    HunonicCmdType type;
    int duration_ms;
    int speed;
    int steps;
    int step_ms;
    int gap_ms;
};

static std::string HunonicToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static int ClampInt(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static HunonicCmdType ActionToType(const std::string& action) {
    std::string a = HunonicToLower(action);
    if (a == "walk_forward" || a == "buoc_toi" || a == "buoc_tien") {
        return HunonicCmdType::WalkForward;
    }
    if (a == "walk_backward" || a == "buoc_lui") {
        return HunonicCmdType::WalkBackward;
    }
    if (a == "walk_left" || a == "buoc_trai") {
        return HunonicCmdType::WalkLeft;
    }
    if (a == "walk_right" || a == "buoc_phai") {
        return HunonicCmdType::WalkRight;
    }
    if (a == "slide_forward" || a == "truot_toi" || a == "truot_tien") {
        return HunonicCmdType::SlideForward;
    }
    if (a == "slide_backward" || a == "truot_lui") {
        return HunonicCmdType::SlideBackward;
    }
    if (a == "slide_left" || a == "truot_trai") {
        return HunonicCmdType::SlideLeft;
    }
    if (a == "slide_right" || a == "truot_phai") {
        return HunonicCmdType::SlideRight;
    }
    if (a == "dance" || a == "nhay_mua") {
        return HunonicCmdType::Dance;
    }
    if (a == "patrol" || a == "tuan_tra") {
        return HunonicCmdType::Patrol;
    }
    return HunonicCmdType::Stop;
}

static bool IsWalk(HunonicCmdType t) {
    return t == HunonicCmdType::WalkForward || t == HunonicCmdType::WalkBackward ||
           t == HunonicCmdType::WalkLeft || t == HunonicCmdType::WalkRight;
}

static bool IsSlide(HunonicCmdType t) {
    return t == HunonicCmdType::SlideForward || t == HunonicCmdType::SlideBackward ||
           t == HunonicCmdType::SlideLeft || t == HunonicCmdType::SlideRight;
}

static HunonicDir TypeToDir(HunonicCmdType t) {
    switch (t) {
        case HunonicCmdType::WalkBackward:
        case HunonicCmdType::SlideBackward:
            return HunonicDir::Backward;
        case HunonicCmdType::WalkLeft:
        case HunonicCmdType::SlideLeft:
            return HunonicDir::Left;
        case HunonicCmdType::WalkRight:
        case HunonicCmdType::SlideRight:
            return HunonicDir::Right;
        default:
            return HunonicDir::Forward;
    }
}

static bool ShowWebQr() {
    auto& wifi = WifiStation::GetInstance();
    if (!wifi.IsConnected()) {
        ESP_LOGW(TAG, "QR skipped: WiFi not connected");
        return false;
    }
    std::string ip = wifi.GetIpAddress();
    std::string url = "http://" + ip + ":" + std::to_string(HUNONIC_WEB_CONTROL_PORT);
    bool ok = qrcode::QRCodeDisplay::GetInstance().Show(url, ip);
    ESP_LOGI(TAG, "Web QR %s: %s", ok ? "shown" : "failed", url.c_str());
    return ok;
}

static void HideWebQr() { qrcode::QRCodeDisplay::GetInstance().Clear(); }

class HunonicController {
private:
    HunonicGait gait_;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    int speed_ = HUNONIC_SPEED_DEFAULT;
    int step_ms_ = HUNONIC_STEP_MS_DEFAULT;
    int steps_ = HUNONIC_STEPS_DEFAULT;
    int duration_ms_ = HUNONIC_DURATION_MS_DEFAULT;
    int gap_ms_ = HUNONIC_GAP_MS_DEFAULT;

    void LoadConfig() {
        Settings settings("hunonic", false);
        speed_ = ClampInt(settings.GetInt("speed", HUNONIC_SPEED_DEFAULT),
                          HUNONIC_SPEED_MIN_EFFECTIVE, 100);
        step_ms_ = ClampInt(settings.GetInt("step_ms", HUNONIC_STEP_MS_DEFAULT), 80, 1200);
        steps_ = ClampInt(settings.GetInt("steps", HUNONIC_STEPS_DEFAULT), 0, 40);
        duration_ms_ = ClampInt(settings.GetInt("duration", HUNONIC_DURATION_MS_DEFAULT), 0, 60000);
        gap_ms_ = ClampInt(settings.GetInt("gap_ms", HUNONIC_GAP_MS_DEFAULT), 0, 200);
        ESP_LOGI(TAG, "Config loaded speed=%d step_ms=%d steps=%d duration=%d gap_ms=%d", speed_,
                 step_ms_, steps_, duration_ms_, gap_ms_);
    }

    void SaveConfig(int speed, int step_ms, int steps, int duration_ms, int gap_ms) {
        speed_ = ClampInt(speed, HUNONIC_SPEED_MIN_EFFECTIVE, 100);
        step_ms_ = ClampInt(step_ms, 80, 1200);
        steps_ = ClampInt(steps, 0, 40);
        duration_ms_ = ClampInt(duration_ms, 0, 60000);
        gap_ms_ = ClampInt(gap_ms, 0, 200);
        Settings settings("hunonic", true);
        settings.SetInt("speed", speed_);
        settings.SetInt("step_ms", step_ms_);
        settings.SetInt("steps", steps_);
        settings.SetInt("duration", duration_ms_);
        settings.SetInt("gap_ms", gap_ms_);
        ESP_LOGI(TAG, "Config saved speed=%d step_ms=%d steps=%d duration=%d gap_ms=%d", speed_,
                 step_ms_, steps_, duration_ms_, gap_ms_);
    }

    bool Enqueue(const HunonicCmd& cmd) {
        if (queue_ == nullptr) {
            return false;
        }
        return xQueueOverwrite(queue_, &cmd) == pdPASS;
    }

    bool WaitOrInterrupt(int ms, HunonicCmd* next) {
        if (ms <= 0) {
            return false;
        }
        TickType_t remaining = pdMS_TO_TICKS(ms);
        while (remaining > 0) {
            TickType_t slice = remaining > pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : remaining;
            if (xQueueReceive(queue_, next, slice) == pdTRUE) {
                return true;
            }
            remaining -= slice;
        }
        return false;
    }

    bool RunWalk(HunonicCmd cmd, HunonicCmd* next) {
        HunonicDir dir = TypeToDir(cmd.type);
        int half = 0;
        const bool until_stop = cmd.steps <= 0;

        while (true) {
            if (!until_stop && half >= cmd.steps) {
                break;
            }

            gait_.WalkHalf(dir, (half % 2) != 0, cmd.speed);
            if (WaitOrInterrupt(cmd.step_ms, next)) {
                gait_.Stop();
                return true;
            }
            gait_.Stop();
            if (cmd.gap_ms > 0 && WaitOrInterrupt(cmd.gap_ms, next)) {
                return true;
            }
            half++;
        }
        gait_.Stop();
        return false;
    }

    bool RunSlide(const HunonicCmd& cmd, HunonicCmd* next) {
        gait_.Slide(TypeToDir(cmd.type), cmd.speed);
        if (cmd.duration_ms <= 0) {
            if (xQueueReceive(queue_, next, portMAX_DELAY) == pdTRUE) {
                return true;
            }
            return false;
        }
        if (WaitOrInterrupt(cmd.duration_ms, next)) {
            return true;
        }
        gait_.Stop();
        return false;
    }

    bool RunPrimitive(const HunonicCmd& cmd, HunonicCmd* next) {
        if (IsWalk(cmd.type)) {
            return RunWalk(cmd, next);
        }
        if (IsSlide(cmd.type)) {
            return RunSlide(cmd, next);
        }
        gait_.Stop();
        return false;
    }

    HunonicCmd Make(HunonicCmdType type, int speed, int steps, int step_ms, int gap_ms,
                    int duration_ms) {
        return HunonicCmd{type, duration_ms, speed, steps, step_ms, gap_ms};
    }

    bool RunDance(const HunonicCmd& cmd, HunonicCmd* next) {
        const int speed = cmd.speed;
        const int step_ms = cmd.step_ms;
        const int gap_ms = cmd.gap_ms;
        HunonicCmd seq[] = {
            Make(HunonicCmdType::WalkForward, speed, 4, step_ms, gap_ms, 0),
            Make(HunonicCmdType::SlideLeft, speed, 0, step_ms, gap_ms, 400),
            Make(HunonicCmdType::SlideRight, speed, 0, step_ms, gap_ms, 400),
            Make(HunonicCmdType::WalkBackward, speed, 4, step_ms, gap_ms, 0),
            Make(HunonicCmdType::WalkLeft, speed, 4, step_ms, gap_ms, 0),
            Make(HunonicCmdType::WalkRight, speed, 4, step_ms, gap_ms, 0),
            Make(HunonicCmdType::SlideForward, speed, 0, step_ms, gap_ms, 500),
            Make(HunonicCmdType::SlideBackward, speed, 0, step_ms, gap_ms, 500),
        };
        for (const auto& step : seq) {
            if (RunPrimitive(step, next)) {
                return true;
            }
        }
        gait_.Stop();
        return false;
    }

    bool RunPatrol(const HunonicCmd& cmd, HunonicCmd* next) {
        const int cycles = cmd.steps > 0 ? cmd.steps : 4;
        HunonicCmd walk_f =
            Make(HunonicCmdType::WalkForward, cmd.speed, 6, cmd.step_ms, cmd.gap_ms, 0);
        HunonicCmd walk_r =
            Make(HunonicCmdType::WalkRight, cmd.speed, 4, cmd.step_ms, cmd.gap_ms, 0);
        for (int i = 0; i < cycles; i++) {
            if (RunPrimitive(walk_f, next) || RunPrimitive(walk_r, next)) {
                return true;
            }
        }
        gait_.Stop();
        return false;
    }

    static void TaskEntry(void* arg) {
        auto* self = static_cast<HunonicController*>(arg);
        HunonicCmd cmd;
        while (1) {
            if (xQueueReceive(self->queue_, &cmd, portMAX_DELAY) != pdTRUE) {
                continue;
            }
            while (true) {
                HunonicCmd next{};
                bool interrupted = false;
                switch (cmd.type) {
                    case HunonicCmdType::Stop:
                        self->gait_.Stop();
                        break;
                    case HunonicCmdType::Dance:
                        interrupted = self->RunDance(cmd, &next);
                        break;
                    case HunonicCmdType::Patrol:
                        interrupted = self->RunPatrol(cmd, &next);
                        break;
                    default:
                        interrupted = self->RunPrimitive(cmd, &next);
                        break;
                }
                if (interrupted) {
                    cmd = next;
                    continue;
                }
                self->gait_.Stop();
                break;
            }
        }
    }

    HunonicCmd CmdFromProps(HunonicCmdType type, const PropertyList& properties) {
        HunonicCmd c{};
        c.type = type;
        c.speed = properties["speed"].value<int>();
        c.duration_ms = properties["duration_ms"].value<int>();
        c.steps = properties["steps"].value<int>();
        c.step_ms = properties["step_ms"].value<int>();
        c.gap_ms = properties["gap_ms"].value<int>();
        if (c.speed < HUNONIC_SPEED_MIN_EFFECTIVE) {
            c.speed = 0;
        }
        c.speed = ClampInt(c.speed, 0, 100);
        c.duration_ms = ClampInt(c.duration_ms, 0, 60000);
        c.steps = ClampInt(c.steps, 0, 40);
        c.step_ms = ClampInt(c.step_ms, 80, 1200);
        c.gap_ms = ClampInt(c.gap_ms, 0, 200);
        if (IsWalk(type)) {
            c.duration_ms = 0;
        }
        return c;
    }

    void RegisterMcpTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool("self.hunonic.stop",
                    "Ricky Hunonic Robot: dung ca hai motor (chan trai va chan phai).",
                    PropertyList(), [this](const PropertyList&) -> ReturnValue {
                        HunonicCmd c{HunonicCmdType::Stop, 0, 0, 0, HUNONIC_STEP_MS_DEFAULT,
                                     HUNONIC_GAP_MS_DEFAULT};
                        if (!Enqueue(c)) {
                            return std::string("queue full");
                        }
                        return true;
                    });

        mcp.AddTool(
            "self.hunonic.action",
            "Ricky Hunonic Robot web/MCP. action="
            "stop|walk_forward|walk_backward|walk_left|walk_right|"
            "slide_forward|slide_backward|slide_left|slide_right|"
            "dance|patrol|show_qrcode|hide_qrcode|save_config|"
            "buoc_toi|buoc_lui|buoc_trai|buoc_phai|"
            "truot_toi|truot_lui|truot_trai|truot_phai|nhay_mua|tuan_tra. "
            "Walk xen ke chan (speed, steps, step_ms). Slide 2 chan dong thoi (speed, duration). "
            "duration chi dung cho truot; 0 = giu den khi stop. save_config ghi NVS.",
            PropertyList({
                Property("action", kPropertyTypeString, "stop"),
                Property("speed", kPropertyTypeInteger, speed_, HUNONIC_SPEED_MIN_EFFECTIVE, 100),
                Property("duration", kPropertyTypeInteger, duration_ms_, 0, 60000),
                Property("steps", kPropertyTypeInteger, steps_, 0, 40),
                Property("step_ms", kPropertyTypeInteger, step_ms_, 80, 1200),
                Property("gap_ms", kPropertyTypeInteger, gap_ms_, 0, 200),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string action = HunonicToLower(properties["action"].value<std::string>());
                if (action == "show_qrcode" || action == "hien_qr" || action == "qr") {
                    return ShowWebQr();
                }
                if (action == "hide_qrcode" || action == "tat_qr") {
                    HideWebQr();
                    return true;
                }
                if (action == "save_config" || action == "luu_cau_hinh") {
                    SaveConfig(properties["speed"].value<int>(), properties["step_ms"].value<int>(),
                               properties["steps"].value<int>(), properties["duration"].value<int>(),
                               properties["gap_ms"].value<int>());
                    return true;
                }

                HunonicCmdType type = ActionToType(action);
                HunonicCmd c{};
                c.type = type;
                c.speed = properties["speed"].value<int>();
                c.duration_ms = properties["duration"].value<int>();
                c.steps = properties["steps"].value<int>();
                c.step_ms = properties["step_ms"].value<int>();
                c.gap_ms = properties["gap_ms"].value<int>();
                if (IsWalk(type)) {
                    c.duration_ms = 0;
                }
                if (type == HunonicCmdType::Stop) {
                    c.speed = 0;
                    c.duration_ms = 0;
                    c.steps = 0;
                }
                if (!Enqueue(c)) {
                    return std::string("queue full");
                }
                return true;
            });

        auto move_props = PropertyList({
            Property("speed", kPropertyTypeInteger, speed_, HUNONIC_SPEED_MIN_EFFECTIVE, 100),
            Property("duration_ms", kPropertyTypeInteger, duration_ms_, 0, 60000),
            Property("steps", kPropertyTypeInteger, steps_, 0, 40),
            Property("step_ms", kPropertyTypeInteger, step_ms_, 80, 1200),
            Property("gap_ms", kPropertyTypeInteger, gap_ms_, 0, 200),
        });

        auto make_handler = [this](HunonicCmdType type) {
            return [this, type](const PropertyList& properties) -> ReturnValue {
                HunonicCmd c = CmdFromProps(type, properties);
                if (!Enqueue(c)) {
                    return std::string("queue full");
                }
                return true;
            };
        };

        mcp.AddTool("self.hunonic.walk_forward",
                    "Ricky Hunonic Robot: buoc tien (chan trai roi chan phai luan phien). "
                    "Chi dung speed, steps, step_ms. steps=so nua-buoc (0 = den khi stop).",
                    move_props, make_handler(HunonicCmdType::WalkForward));
        mcp.AddTool("self.hunonic.walk_backward",
                    "Ricky Hunonic Robot: buoc lui (luan phien chan). Tham so giong walk_forward.",
                    move_props, make_handler(HunonicCmdType::WalkBackward));
        mcp.AddTool("self.hunonic.walk_left",
                    "Ricky Hunonic Robot: buoc xoay trai (chan trai lui, chan phai toi, xen ke).",
                    move_props, make_handler(HunonicCmdType::WalkLeft));
        mcp.AddTool("self.hunonic.walk_right",
                    "Ricky Hunonic Robot: buoc xoay phai (chan trai toi, chan phai lui, xen ke).",
                    move_props, make_handler(HunonicCmdType::WalkRight));
        mcp.AddTool("self.hunonic.slide_forward",
                    "Ricky Hunonic Robot: truot tien (2 chan quay toi dong thoi). "
                    "duration_ms=0 chay den khi stop.",
                    move_props, make_handler(HunonicCmdType::SlideForward));
        mcp.AddTool("self.hunonic.slide_backward",
                    "Ricky Hunonic Robot: truot lui (2 chan dong thoi). Tham so giong slide_forward.",
                    move_props, make_handler(HunonicCmdType::SlideBackward));
        mcp.AddTool("self.hunonic.slide_left",
                    "Ricky Hunonic Robot: truot xoay trai (trai lui + phai toi dong thoi).",
                    move_props, make_handler(HunonicCmdType::SlideLeft));
        mcp.AddTool("self.hunonic.slide_right",
                    "Ricky Hunonic Robot: truot xoay phai (trai toi + phai lui dong thoi).",
                    move_props, make_handler(HunonicCmdType::SlideRight));

        mcp.AddTool(
            "self.hunonic.dance",
            "Ricky Hunonic Robot: nhay mua — chuoi buoc/truot co ban. Dung self.hunonic.stop de cat.",
            move_props, make_handler(HunonicCmdType::Dance));
        mcp.AddTool(
            "self.hunonic.patrol",
            "Ricky Hunonic Robot: di tuan tra (buoc toi roi buoc phai, lap). "
            "steps = so vong (mac dinh 4). Dung stop de cat.",
            move_props, make_handler(HunonicCmdType::Patrol));

        mcp.AddTool(
            "self.hunonic.show_qrcode",
            "Hien ma QR link web dieu khien robot tren man hinh OLED. "
            "Dung khi user noi: hien ma QR, show QR, quet ma vao web, mo web control, "
            "hien link dieu khien, QR code web. URL = http://<IP>:8080. "
            "Dien thoai phai cung WiFi voi robot.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                auto& wifi = WifiStation::GetInstance();
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "connected", wifi.IsConnected());
                if (!wifi.IsConnected()) {
                    cJSON_AddStringToObject(json, "message", "Device is not connected to WiFi");
                    return json;
                }
                std::string ip = wifi.GetIpAddress();
                std::string url =
                    "http://" + ip + ":" + std::to_string(HUNONIC_WEB_CONTROL_PORT);
                bool ok = qrcode::QRCodeDisplay::GetInstance().Show(url, ip);
                cJSON_AddStringToObject(json, "ip_address", ip.c_str());
                cJSON_AddStringToObject(json, "url", url.c_str());
                cJSON_AddBoolToObject(json, "qrcode_displayed", ok);
                return json;
            });

        mcp.AddTool("self.hunonic.hide_qrcode",
                    "Tat ma QR tren man hinh, tra lai mat robot. "
                    "Dung khi user noi: tat QR, an ma QR, ve mat.",
                    PropertyList(), [](const PropertyList&) -> ReturnValue {
                        HideWebQr();
                        return true;
                    });

        mcp.AddTool(
            "self.hunonic.save_config",
            "Luu thong so gait len robot (NVS): speed, step_ms, steps, duration, gap_ms. "
            "Dung khi user noi: luu cau hinh, save config.",
            PropertyList({
                Property("speed", kPropertyTypeInteger, speed_, HUNONIC_SPEED_MIN_EFFECTIVE, 100),
                Property("step_ms", kPropertyTypeInteger, step_ms_, 80, 1200),
                Property("steps", kPropertyTypeInteger, steps_, 0, 40),
                Property("duration", kPropertyTypeInteger, duration_ms_, 0, 60000),
                Property("gap_ms", kPropertyTypeInteger, gap_ms_, 0, 200),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                SaveConfig(properties["speed"].value<int>(), properties["step_ms"].value<int>(),
                           properties["steps"].value<int>(), properties["duration"].value<int>(),
                           properties["gap_ms"].value<int>());
                return true;
            });
    }

public:
    void FillWebConfig(HunonicWebConfig* out) const {
        if (out == nullptr) {
            return;
        }
        out->speed = speed_;
        out->step_ms = step_ms_;
        out->steps = steps_;
        out->duration_ms = duration_ms_;
        out->gap_ms = gap_ms_;
    }

    HunonicController() {
        gait_.Init();
        LoadConfig();
        queue_ = xQueueCreate(1, sizeof(HunonicCmd));
        xTaskCreate(TaskEntry, "hunonic_gait", 4096, this, 5, &task_handle_);
        RegisterMcpTools();
        ESP_LOGI(TAG, "Hunonic controller ready, fw %s", RICKY_HUNONIC_ROBOT_VERSION);
    }
};

static HunonicController* s_controller = nullptr;

void InitializeHunonicController() {
    if (s_controller == nullptr) {
        s_controller = new HunonicController();
    }
}

bool HunonicGetWebConfig(HunonicWebConfig* out) {
    if (s_controller == nullptr || out == nullptr) {
        return false;
    }
    s_controller->FillWebConfig(out);
    return true;
}
