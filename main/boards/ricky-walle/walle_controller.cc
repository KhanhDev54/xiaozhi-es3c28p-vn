#include <esp_log.h>
#include <wifi_station.h>

#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "power_manager.h"
#include "settings.h"
#include "walle_movements.h"

#define TAG "WallEController"

class WallEController {
private:
    WallE walle_;
    TaskHandle_t action_task_handle_ = nullptr;
    QueueHandle_t action_queue_;
    bool is_action_in_progress_ = false;

    struct ActionParams {
        int action_type;
        int steps;
        int speed;
        int direction;
        int amount;
        int duration;
    };

    enum ActionType {
        ACTION_FORWARD = 1,
        ACTION_BACKWARD = 2,
        ACTION_TURN_LEFT = 3,
        ACTION_TURN_RIGHT = 4,
        ACTION_STOP = 5,
        ACTION_ARMS_UP = 6,
        ACTION_ARMS_DOWN = 7,
        ACTION_ARMS_WAVE = 8,
        ACTION_LOOK_LEFT = 9,
        ACTION_LOOK_RIGHT = 10,
        ACTION_LOOK_CENTER = 11,
        ACTION_NOD = 12,
        ACTION_HOME = 13,
    };

    static void ActionTask(void* arg) {
        WallEController* controller = static_cast<WallEController*>(arg);
        ActionParams params;
        controller->walle_.AttachServos();

        while (true) {
            if (xQueueReceive(controller->action_queue_, &params, pdMS_TO_TICKS(1000)) == pdTRUE) {
                ESP_LOGI(TAG, "Run action %d", params.action_type);
                PowerManager::PauseBatteryUpdate();
                controller->is_action_in_progress_ = true;

                int dir = params.direction;
                int speed = params.speed;
                int duration = params.duration;
                int amount = params.amount;
                int steps = params.steps;

                switch (params.action_type) {
                    case ACTION_FORWARD:
                        controller->walle_.Forward(speed, duration);
                        break;
                    case ACTION_BACKWARD:
                        controller->walle_.Backward(speed, duration);
                        break;
                    case ACTION_TURN_LEFT:
                        controller->walle_.TurnLeft(speed, duration);
                        break;
                    case ACTION_TURN_RIGHT:
                        controller->walle_.TurnRight(speed, duration);
                        break;
                    case ACTION_STOP:
                        controller->walle_.StopTracks();
                        break;
                    case ACTION_ARMS_UP:
                        controller->walle_.ArmsUp(duration, dir);
                        break;
                    case ACTION_ARMS_DOWN:
                        controller->walle_.ArmsDown(duration, dir);
                        break;
                    case ACTION_ARMS_WAVE:
                        controller->walle_.ArmsWave(steps, duration, dir);
                        break;
                    case ACTION_LOOK_LEFT:
                        controller->walle_.LookLeft(amount, duration);
                        break;
                    case ACTION_LOOK_RIGHT:
                        controller->walle_.LookRight(amount, duration);
                        break;
                    case ACTION_LOOK_CENTER:
                        controller->walle_.LookCenter(duration);
                        break;
                    case ACTION_NOD:
                        controller->walle_.Nod(steps, amount, duration);
                        break;
                    case ACTION_HOME:
                        controller->walle_.Home();
                        break;
                    default:
                        ESP_LOGW(TAG, "Unknown action %d", params.action_type);
                        break;
                }

                controller->is_action_in_progress_ = false;
                PowerManager::ResumeBatteryUpdate();
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void StartActionTaskIfNeeded() {
        if (action_task_handle_ == nullptr) {
            xTaskCreate(ActionTask, "walle_action", 1024 * 3, this, configMAX_PRIORITIES - 1,
                        &action_task_handle_);
        }
    }

    void QueueAction(int action_type, int steps, int speed, int direction, int amount,
                     int duration) {
        ESP_LOGI(TAG, "Queue action=%d steps=%d speed=%d dir=%d amount=%d duration=%d", action_type,
                 steps, speed, direction, amount, duration);
        ActionParams params = {action_type, steps, speed, direction, amount, duration};
        xQueueSend(action_queue_, &params, portMAX_DELAY);
        StartActionTaskIfNeeded();
    }

    void LoadTrimsFromNVS() {
        Settings settings("walle_trims", false);
        int left_track = settings.GetInt("left_track", 0);
        int right_track = settings.GetInt("right_track", 0);
        int left_arm = settings.GetInt("left_arm", 0);
        int right_arm = settings.GetInt("right_arm", 0);
        int head = settings.GetInt("head", 0);
        ESP_LOGI(TAG, "Trims: lt=%d rt=%d la=%d ra=%d head=%d", left_track, right_track, left_arm,
                 right_arm, head);
        walle_.SetTrims(left_track, right_track, left_arm, right_arm, head);
    }

public:
    WallEController() {
        walle_.Init(LEFT_TRACK_PIN, RIGHT_TRACK_PIN, LEFT_ARM_PIN, RIGHT_ARM_PIN, HEAD_PIN);
        LoadTrimsFromNVS();
        action_queue_ = xQueueCreate(10, sizeof(ActionParams));
        QueueAction(ACTION_HOME, 1, 50, 0, 0, 500);
        RegisterMcpTools();
    }

    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();
        ESP_LOGI(TAG, "Registering Wall-E MCP tools");

        mcp_server.AddTool(
            "self.walle.action",
            "Dieu khien robot Wall-E. action: ten hanh dong. "
            "Di chuyen (servo 360 do): forward, backward, turn_left, turn_right, stop. "
            "speed 10-90 (mac dinh 50), duration 0-20000ms (0 = chay lien tuc den khi stop). "
            "Tay (servo 180 do): arms_up, arms_down, arms_wave. "
            "direction: 1=tay trai, -1=tay phai, 0=hai tay. steps 1-10 cho wave. "
            "Dau (servo 180 do): look_left, look_right, look_center, nod. "
            "amount 10-80 goc dau. home: ve tu the nghi, banh xich dung.",
            PropertyList({Property("action", kPropertyTypeString, "home"),
                          Property("steps", kPropertyTypeInteger, 3, 1, 10),
                          Property("speed", kPropertyTypeInteger, 50, 10, 90),
                          Property("direction", kPropertyTypeInteger, 0, -1, 1),
                          Property("amount", kPropertyTypeInteger, 30, 10, 80),
                          Property("duration", kPropertyTypeInteger, 800, 0, 20000)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string action = properties["action"].value<std::string>();
                int steps = properties["steps"].value<int>();
                int speed = properties["speed"].value<int>();
                int direction = properties["direction"].value<int>();
                int amount = properties["amount"].value<int>();
                int duration = properties["duration"].value<int>();

                if (action == "forward") {
                    QueueAction(ACTION_FORWARD, steps, speed, 1, amount, duration);
                } else if (action == "backward") {
                    QueueAction(ACTION_BACKWARD, steps, speed, -1, amount, duration);
                } else if (action == "turn_left") {
                    QueueAction(ACTION_TURN_LEFT, steps, speed, 1, amount, duration);
                } else if (action == "turn_right") {
                    QueueAction(ACTION_TURN_RIGHT, steps, speed, -1, amount, duration);
                } else if (action == "stop") {
                    QueueAction(ACTION_STOP, 1, 0, 0, 0, 0);
                } else if (action == "arms_up") {
                    QueueAction(ACTION_ARMS_UP, 1, speed, direction, amount, duration);
                } else if (action == "arms_down") {
                    QueueAction(ACTION_ARMS_DOWN, 1, speed, direction, amount, duration);
                } else if (action == "arms_wave") {
                    QueueAction(ACTION_ARMS_WAVE, steps, speed, direction, amount, duration);
                } else if (action == "look_left") {
                    QueueAction(ACTION_LOOK_LEFT, 1, speed, 1, amount, duration);
                } else if (action == "look_right") {
                    QueueAction(ACTION_LOOK_RIGHT, 1, speed, -1, amount, duration);
                } else if (action == "look_center") {
                    QueueAction(ACTION_LOOK_CENTER, 1, speed, 0, amount, duration);
                } else if (action == "nod") {
                    QueueAction(ACTION_NOD, steps, speed, 0, amount, duration);
                } else if (action == "home") {
                    QueueAction(ACTION_HOME, 1, 50, 0, 0, 500);
                } else {
                    return "Loi: action khong hop le. Dung: forward, backward, turn_left, "
                           "turn_right, stop, arms_up, arms_down, arms_wave, look_left, "
                           "look_right, look_center, nod, home";
                }
                return true;
            });

        mcp_server.AddTool(
            "self.walle.stop", "Dung ngay banh xich va ve tu the nghi", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                if (action_task_handle_ != nullptr) {
                    vTaskDelete(action_task_handle_);
                    action_task_handle_ = nullptr;
                }
                is_action_in_progress_ = false;
                PowerManager::ResumeBatteryUpdate();
                xQueueReset(action_queue_);
                walle_.StopTracks();
                QueueAction(ACTION_HOME, 1, 50, 0, 0, 500);
                return true;
            });

        mcp_server.AddTool(
            "self.walle.set_trim",
            "Hieu chinh servo. servo_type: left_track/right_track/left_arm/right_arm/head; "
            "trim_value: -50 den 50. Track trim de dung dung o 90 do.",
            PropertyList({Property("servo_type", kPropertyTypeString, "head"),
                          Property("trim_value", kPropertyTypeInteger, 0, -50, 50)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string servo_type = properties["servo_type"].value<std::string>();
                int trim_value = properties["trim_value"].value<int>();

                Settings settings("walle_trims", true);
                int left_track = settings.GetInt("left_track", 0);
                int right_track = settings.GetInt("right_track", 0);
                int left_arm = settings.GetInt("left_arm", 0);
                int right_arm = settings.GetInt("right_arm", 0);
                int head = settings.GetInt("head", 0);

                if (servo_type == "left_track") {
                    left_track = trim_value;
                    settings.SetInt("left_track", left_track);
                } else if (servo_type == "right_track") {
                    right_track = trim_value;
                    settings.SetInt("right_track", right_track);
                } else if (servo_type == "left_arm") {
                    left_arm = trim_value;
                    settings.SetInt("left_arm", left_arm);
                } else if (servo_type == "right_arm") {
                    right_arm = trim_value;
                    settings.SetInt("right_arm", right_arm);
                } else if (servo_type == "head") {
                    head = trim_value;
                    settings.SetInt("head", head);
                } else {
                    return "Loi: servo_type phai la left_track, right_track, left_arm, right_arm, "
                           "head";
                }

                walle_.SetTrims(left_track, right_track, left_arm, right_arm, head);
                QueueAction(ACTION_HOME, 1, 50, 0, 0, 500);
                return "Trim " + servo_type + " = " + std::to_string(trim_value);
            });

        mcp_server.AddTool("self.walle.get_trims", "Lay trim servo hien tai", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               Settings settings("walle_trims", false);
                               return std::string("{\"left_track\":") +
                                      std::to_string(settings.GetInt("left_track", 0)) +
                                      ",\"right_track\":" +
                                      std::to_string(settings.GetInt("right_track", 0)) +
                                      ",\"left_arm\":" +
                                      std::to_string(settings.GetInt("left_arm", 0)) +
                                      ",\"right_arm\":" +
                                      std::to_string(settings.GetInt("right_arm", 0)) +
                                      ",\"head\":" + std::to_string(settings.GetInt("head", 0)) +
                                      "}";
                           });

        mcp_server.AddTool("self.walle.get_status", "Trang thai robot: moving hoac idle",
                           PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
                               return is_action_in_progress_ ? "moving" : "idle";
                           });

        mcp_server.AddTool("self.battery.get_level", "Pin va trang thai sac", PropertyList(),
                           [](const PropertyList& properties) -> ReturnValue {
                               auto& board = Board::GetInstance();
                               int level = 0;
                               bool charging = false;
                               bool discharging = false;
                               board.GetBatteryLevel(level, charging, discharging);
                               return std::string("{\"level\":") + std::to_string(level) +
                                      ",\"charging\":" + (charging ? "true" : "false") + "}";
                           });

        mcp_server.AddTool("self.walle.get_ip", "Dia chi WiFi IP", PropertyList(),
                           [](const PropertyList& properties) -> ReturnValue {
                               auto& wifi_station = WifiStation::GetInstance();
                               std::string ip = wifi_station.GetIpAddress();
                               if (ip.empty()) {
                                   return "{\"ip\":\"\",\"connected\":false}";
                               }
                               return std::string("{\"ip\":\"") + ip + "\",\"connected\":true}";
                           });

        ESP_LOGI(TAG, "Wall-E MCP tools registered");
    }

    ~WallEController() {
        if (action_task_handle_ != nullptr) {
            vTaskDelete(action_task_handle_);
            action_task_handle_ = nullptr;
        }
        vQueueDelete(action_queue_);
    }
};

static WallEController* g_walle_controller = nullptr;

void InitializeWallEController() {
    if (g_walle_controller == nullptr) {
        g_walle_controller = new WallEController();
        ESP_LOGI(TAG, "Wall-E controller initialized");
    }
}
