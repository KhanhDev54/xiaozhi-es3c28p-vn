#include <algorithm>
#include <atomic>
#include <cctype>
#include <cJSON.h>
#include <esp_log.h>
#include <string>
#include <string_view>

#include <wifi_station.h>

#include "application.h"
#include "config.h"
#include "device_state.h"
#include "features/QRCode/qrcode_display.h"
#include "mcp_server.h"
#include "tank_controller.h"
#include "tank_drive.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern const char tank_running_ogg_start[] asm("_binary_tank_running_ogg_start");
extern const char tank_running_ogg_end[] asm("_binary_tank_running_ogg_end");

static std::string_view TankRunningOgg() {
    return {tank_running_ogg_start,
            static_cast<size_t>(tank_running_ogg_end - tank_running_ogg_start)};
}

#define TAG "TankController"

enum class TankCmdType : uint8_t {
    Stop = 0,
    ForwardMs,
    BackwardMs,
    TurnLeftMs,
    TurnRightMs,
    SpinLeftMs,
    SpinRightMs,
    Dance,
    Patrol,
};

struct TankCmd {
    TankCmdType type;
    int duration_ms;
    int speed;
};

static std::string TankToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static int TankResolveSpeed(const PropertyList& pl) {
    std::string pace = TankToLower(pl["pace"].value<std::string>());
    if (pace == "slow" || pace == "cham") {
        return TANK_SPEED_MIN_EFFECTIVE + 10;
    }
    if (pace == "fast" || pace == "nhanh") {
        return 85;
    }
    if (pace == "custom" || pace == "manual") {
        int v = pl["speed"].value<int>();
        if (v < TANK_SPEED_MIN_EFFECTIVE) {
            return 0;
        }
        if (v > 100) {
            v = 100;
        }
        return v;
    }
    return TANK_SPEED_DEFAULT;
}

static TankCmdType TankActionToType(const std::string& action) {
    std::string a = TankToLower(action);
    if (a == "forward" || a == "tien") {
        return TankCmdType::ForwardMs;
    }
    if (a == "backward" || a == "lui") {
        return TankCmdType::BackwardMs;
    }
    if (a == "turn_left" || a == "re_trai") {
        return TankCmdType::TurnLeftMs;
    }
    if (a == "turn_right" || a == "re_phai") {
        return TankCmdType::TurnRightMs;
    }
    if (a == "spin_left" || a == "xoay_trai") {
        return TankCmdType::SpinLeftMs;
    }
    if (a == "spin_right" || a == "xoay_phai") {
        return TankCmdType::SpinRightMs;
    }
    if (a == "dance" || a == "nhay" || a == "nhay_mua") {
        return TankCmdType::Dance;
    }
    if (a == "patrol" || a == "tuan_tra") {
        return TankCmdType::Patrol;
    }
    return TankCmdType::Stop;
}

static bool ShowWebQr() {
    std::string ip = WifiStation::GetInstance().GetIpAddress();
    if (ip.empty()) {
        ESP_LOGW(TAG, "No IP — cannot show QR");
        return false;
    }
    std::string url = "http://" + ip + ":" + std::to_string(TANK_WEB_CONTROL_PORT);
    bool ok = qrcode::QRCodeDisplay::GetInstance().Show(url, ip + ":" + std::to_string(TANK_WEB_CONTROL_PORT));
    ESP_LOGI(TAG, "Web QR %s: %s", ok ? "shown" : "failed", url.c_str());
    return ok;
}

static void HideWebQr() { qrcode::QRCodeDisplay::GetInstance().Clear(); }

enum class TankSfxMode : uint8_t { Idle = 0, Running };

class TankController {
private:
    TankDrive drive_;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    TaskHandle_t sfx_task_handle_ = nullptr;
    std::atomic<TankSfxMode> sfx_mode_{TankSfxMode::Idle};
    std::atomic<bool> sound_enabled_{false};

    static bool VoiceBusy() {
        DeviceState s = Application::GetInstance().GetDeviceState();
        return s == kDeviceStateListening || s == kDeviceStateSpeaking ||
               s == kDeviceStateConnecting;
    }

    void StopSfxPlayback() {
        Application::GetInstance().Schedule([]() {
            Application::GetInstance().GetAudioService().ResetDecoder();
        });
    }

    void PlayRunningIfEnabled() {
        if (!sound_enabled_.load()) {
            return;
        }
        Application::GetInstance().Schedule([]() {
            auto& app = Application::GetInstance();
            app.GetAudioService().ResetDecoder();
            app.GetAudioService().UpdateOutputTimestamp();
            app.PlaySound(TankRunningOgg());
            ESP_LOGI(TAG, "SFX -> running");
        });
    }

    void SetSfx(TankSfxMode mode) {
        TankSfxMode prev = sfx_mode_.exchange(mode);
        if (prev == mode) {
            return;
        }
        if (mode == TankSfxMode::Running) {
            PlayRunningIfEnabled();
            return;
        }
        if (!VoiceBusy()) {
            StopSfxPlayback();
        }
    }

    static void SfxTask(void* arg) {
        auto* self = static_cast<TankController*>(arg);
        auto& app = Application::GetInstance();

        while (app.GetDeviceState() != kDeviceStateIdle &&
               app.GetDeviceState() != kDeviceStateListening &&
               app.GetDeviceState() != kDeviceStateSpeaking) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        ESP_LOGI(TAG, "Tank SFX loop started (sound default OFF)");

        while (1) {
            if (self->sfx_mode_.load() != TankSfxMode::Running || !self->sound_enabled_.load()) {
                vTaskDelay(pdMS_TO_TICKS(150));
                continue;
            }
            if (!app.GetAudioService().IsIdle()) {
                vTaskDelay(pdMS_TO_TICKS(40));
                continue;
            }
            app.GetAudioService().UpdateOutputTimestamp();
            app.PlaySound(TankRunningOgg());
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }

    bool Enqueue(const TankCmd& cmd) {
        if (queue_ == nullptr) {
            return false;
        }
        return xQueueOverwrite(queue_, &cmd) == pdPASS;
    }

    void Apply(const TankCmd& cmd) {
        switch (cmd.type) {
            case TankCmdType::Stop:
            case TankCmdType::Dance:
            case TankCmdType::Patrol:
                drive_.Stop();
                SetSfx(TankSfxMode::Idle);
                break;
            case TankCmdType::ForwardMs:
                drive_.Forward(cmd.speed);
                SetSfx(TankSfxMode::Running);
                break;
            case TankCmdType::BackwardMs:
                drive_.Backward(cmd.speed);
                SetSfx(TankSfxMode::Running);
                break;
            case TankCmdType::TurnLeftMs:
                drive_.TurnLeft(cmd.speed);
                SetSfx(TankSfxMode::Running);
                break;
            case TankCmdType::TurnRightMs:
                drive_.TurnRight(cmd.speed);
                SetSfx(TankSfxMode::Running);
                break;
            case TankCmdType::SpinLeftMs:
                drive_.SpinLeft(cmd.speed);
                SetSfx(TankSfxMode::Running);
                break;
            case TankCmdType::SpinRightMs:
                drive_.SpinRight(cmd.speed);
                SetSfx(TankSfxMode::Running);
                break;
        }
    }

    bool WaitOrInterrupt(int duration_ms, TankCmd* next) {
        if (duration_ms <= 0) {
            return false;
        }
        TickType_t remaining = pdMS_TO_TICKS(duration_ms);
        while (remaining > 0) {
            TickType_t slice = remaining > pdMS_TO_TICKS(20) ? pdMS_TO_TICKS(20) : remaining;
            if (xQueueReceive(queue_, next, slice) == pdTRUE) {
                return true;
            }
            remaining -= slice;
        }
        return false;
    }

    bool RunMove(TankCmdType type, int speed, int duration_ms, TankCmd* next) {
        Apply(TankCmd{type, duration_ms, speed});
        if (WaitOrInterrupt(duration_ms, next)) {
            return true;
        }
        drive_.Stop();
        SetSfx(TankSfxMode::Idle);
        return false;
    }

    bool RunDance(const TankCmd& cmd, TankCmd* next) {
        const int speed = cmd.speed;
        ESP_LOGI(TAG, "Dance start speed=%d", speed);
        struct Step {
            TankCmdType type;
            int ms;
        };
        const Step seq[] = {
            {TankCmdType::ForwardMs, 400},  {TankCmdType::SpinLeftMs, 350},
            {TankCmdType::SpinRightMs, 350}, {TankCmdType::BackwardMs, 400},
            {TankCmdType::TurnLeftMs, 300},  {TankCmdType::TurnRightMs, 300},
            {TankCmdType::SpinLeftMs, 250},  {TankCmdType::SpinRightMs, 250},
        };
        for (const auto& step : seq) {
            if (RunMove(step.type, speed, step.ms, next)) {
                return true;
            }
        }
        return false;
    }

    bool RunPatrol(const TankCmd& cmd, TankCmd* next) {
        const int speed = cmd.speed;
        int cycles = 4;
        if (cmd.duration_ms >= 1 && cmd.duration_ms <= 12) {
            cycles = cmd.duration_ms;
        }
        ESP_LOGI(TAG, "Patrol start speed=%d cycles=%d", speed, cycles);
        for (int i = 0; i < cycles; i++) {
            if (RunMove(TankCmdType::ForwardMs, speed, 800, next) ||
                RunMove(TankCmdType::SpinRightMs, speed, 500, next)) {
                return true;
            }
        }
        return false;
    }

    static void TaskEntry(void* arg) {
        auto* self = static_cast<TankController*>(arg);
        TankCmd cmd;
        while (1) {
            if (xQueueReceive(self->queue_, &cmd, portMAX_DELAY) != pdTRUE) {
                continue;
            }

            while (true) {
                TankCmd next{};
                bool interrupted = false;

                if (cmd.type == TankCmdType::Dance) {
                    interrupted = self->RunDance(cmd, &next);
                } else if (cmd.type == TankCmdType::Patrol) {
                    interrupted = self->RunPatrol(cmd, &next);
                } else {
                    self->Apply(cmd);
                    if (cmd.type == TankCmdType::Stop || cmd.duration_ms <= 0) {
                        break;
                    }
                    interrupted = self->WaitOrInterrupt(cmd.duration_ms, &next);
                    if (!interrupted) {
                        self->drive_.Stop();
                        self->SetSfx(TankSfxMode::Idle);
                        break;
                    }
                }

                if (interrupted) {
                    cmd = next;
                    continue;
                }
                self->drive_.Stop();
                self->SetSfx(TankSfxMode::Idle);
                break;
            }
        }
    }

    void RegisterMcpTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool("self.tank.stop",
                    "Ricky War Tank: dừng cả hai motor N20 (Motor A trái, Motor B phải).",
                    PropertyList(), [this](const PropertyList&) -> ReturnValue {
                        TankCmd c{TankCmdType::Stop, 0, 0};
                        if (!Enqueue(c)) {
                            return std::string("queue full");
                        }
                        return true;
                    });

        mcp.AddTool(
            "self.tank.action",
            "Ricky War Tank web/MCP: action=stop|forward|backward|turn_left|turn_right|"
            "spin_left|spin_right|dance|patrol|sound_on|sound_off|bat_am|tat_am|"
            "show_qrcode|hide_qrcode. "
            "speed 30-100. duration 0-60000ms (0 = chạy đến khi stop).",
            PropertyList({
                Property("action", kPropertyTypeString, "stop"),
                Property("speed", kPropertyTypeInteger, TANK_SPEED_DEFAULT, TANK_SPEED_MIN_EFFECTIVE,
                         100),
                Property("duration", kPropertyTypeInteger, 800, 0, 60000),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string action = properties["action"].value<std::string>();
                std::string a = TankToLower(action);
                if (a == "sound_on" || a == "bat_am" || a == "mo_dong_co" || a == "unlock") {
                    SetSoundEnabled(true);
                    return true;
                }
                if (a == "sound_off" || a == "tat_am" || a == "lock") {
                    SetSoundEnabled(false);
                    return true;
                }
                if (a == "show_qrcode" || a == "hien_qr" || a == "qr") {
                    return ShowWebQr();
                }
                if (a == "hide_qrcode" || a == "tat_qr") {
                    HideWebQr();
                    return true;
                }

                TankCmdType type = TankActionToType(action);
                int speed = properties["speed"].value<int>();
                int duration = properties["duration"].value<int>();
                if (type == TankCmdType::Stop) {
                    speed = 0;
                    duration = 0;
                }
                TankCmd c{type, duration, speed};
                if (!Enqueue(c)) {
                    return std::string("queue full");
                }
                return true;
            });

        auto move_props = PropertyList({
            Property("pace", kPropertyTypeString, "normal"),
            Property("duration_ms", kPropertyTypeInteger, 1500, 0, 60000),
            Property("speed", kPropertyTypeInteger, TANK_SPEED_DEFAULT, TANK_SPEED_MIN_EFFECTIVE, 100),
        });

        auto make_move_handler = [this](TankCmdType type) {
            return [this, type](const PropertyList& properties) -> ReturnValue {
                TankCmd c{type, properties["duration_ms"].value<int>(), TankResolveSpeed(properties)};
                if (!Enqueue(c)) {
                    return std::string("queue full");
                }
                return true;
            };
        };

        mcp.AddTool("self.tank.forward",
                    "Ricky War Tank: tiến thẳng. pace=normal|slow|fast|custom, speed khi custom "
                    "(30-100). duration_ms=0 chạy đến khi stop.",
                    move_props, make_move_handler(TankCmdType::ForwardMs));

        mcp.AddTool("self.tank.backward",
                    "Ricky War Tank: lùi thẳng. Tham số giống self.tank.forward.",
                    move_props, make_move_handler(TankCmdType::BackwardMs));

        mcp.AddTool("self.tank.turn_left",
                    "Ricky War Tank: rẽ trái tại chỗ (bánh trái dừng, bánh phải tiến).",
                    move_props, make_move_handler(TankCmdType::TurnLeftMs));

        mcp.AddTool("self.tank.turn_right",
                    "Ricky War Tank: rẽ phải tại chỗ (bánh phải dừng, bánh trái tiến).",
                    move_props, make_move_handler(TankCmdType::TurnRightMs));

        mcp.AddTool("self.tank.spin_left",
                    "Ricky War Tank: xoay trái tại chỗ (bánh trái lùi, bánh phải tiến).",
                    move_props, make_move_handler(TankCmdType::SpinLeftMs));

        mcp.AddTool("self.tank.spin_right",
                    "Ricky War Tank: xoay phải tại chỗ (bánh trái tiến, bánh phải lùi).",
                    move_props, make_move_handler(TankCmdType::SpinRightMs));

        mcp.AddTool(
            "self.tank.dance",
            "Ricky War Tank: nhảy múa — chuỗi tiến/xoay/lùi/rẽ. Dùng self.tank.stop để cắt. "
            "Khi user nói: nhảy, nhảy múa, dance.",
            move_props, make_move_handler(TankCmdType::Dance));

        mcp.AddTool(
            "self.tank.patrol",
            "Ricky War Tank: tuần tra — tiến rồi xoay phải, lặp 4 vòng. Dùng stop để cắt. "
            "Khi user nói: tuần tra, patrol, đi tuần.",
            move_props, make_move_handler(TankCmdType::Patrol));

        mcp.AddTool(
            "self.tank.set_sound",
            "Ricky War Tank: bật/tắt tiếng máy khi chạy. Mặc định tắt. "
            "enabled=true khi user nói bật âm thanh / mở động cơ; false khi tắt âm.",
            PropertyList({Property("enabled", kPropertyTypeBoolean, false)}),
            [this](const PropertyList& properties) -> ReturnValue {
                SetSoundEnabled(properties["enabled"].value<bool>());
                return true;
            });

        mcp.AddTool(
            "self.tank.show_qrcode",
            "Hien ma QR link web dieu khien xe tang tren man hinh OLED. "
            "Dung khi user noi: hien ma QR, show QR, quet ma vao web, mo web control, "
            "hien link dieu khien, QR code web. URL = http://<IP>:8081. "
            "Dien thoai phai cung WiFi voi xe tang.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                std::string ip = WifiStation::GetInstance().GetIpAddress();
                if (ip.empty()) {
                    return std::string("{\"success\":false,\"message\":\"No WiFi connection\"}");
                }
                std::string url = "http://" + ip + ":" + std::to_string(TANK_WEB_CONTROL_PORT);
                bool ok = qrcode::QRCodeDisplay::GetInstance().Show(
                    url, ip + ":" + std::to_string(TANK_WEB_CONTROL_PORT));
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "ip_address", ip.c_str());
                cJSON_AddStringToObject(json, "url", url.c_str());
                cJSON_AddBoolToObject(json, "qrcode_displayed", ok);
                return json;
            });

        mcp.AddTool(
            "self.tank.hide_qrcode",
            "An ma QR tren man hinh. Dung khi user noi: tat QR, an QR, dong QR.",
            PropertyList(), [](const PropertyList&) -> ReturnValue {
                HideWebQr();
                return true;
            });
    }

public:
    bool IsSoundEnabled() const { return sound_enabled_.load(); }

    void SetSoundEnabled(bool enabled) {
        sound_enabled_.store(enabled);
        ESP_LOGI(TAG, "Sound %s", enabled ? "ON" : "OFF");
        if (!enabled) {
            if (!VoiceBusy()) {
                StopSfxPlayback();
            }
            return;
        }
        if (sfx_mode_.load() == TankSfxMode::Running) {
            PlayRunningIfEnabled();
        }
    }

    TankController() {
        drive_.Init();
        queue_ = xQueueCreate(1, sizeof(TankCmd));
        xTaskCreate(TaskEntry, "tank_drive", 4096, this, 5, &task_handle_);
        xTaskCreate(SfxTask, "tank_sfx", 4096, this, 3, &sfx_task_handle_);
        RegisterMcpTools();
        ESP_LOGI(TAG, "Tank controller ready, fw %s", RICKY_WAR_TANK_VERSION);
    }
};

static TankController* s_controller = nullptr;

void InitializeTankController() {
    if (s_controller == nullptr) {
        s_controller = new TankController();
    }
}

bool TankSoundEnabled() {
    return s_controller != nullptr && s_controller->IsSoundEnabled();
}

void TankSetSoundEnabled(bool enabled) {
    if (s_controller != nullptr) {
        s_controller->SetSoundEnabled(enabled);
    }
}
