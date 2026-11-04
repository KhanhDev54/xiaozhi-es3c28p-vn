#ifndef __IR_CONTROLLER_H__
#define __IR_CONTROLLER_H__

#include "mcp_server.h"
#include "config.h"
#include "ir_nec_encoder.h"
#include "ir_raw_tx.h"
#include "ir_robot_type.h"
#include "dog_ir_codes.h"
#include "r839_ir_codes.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/rmt_tx.h>
#include <stdexcept>
#include <string>

#define IR_RESOLUTION_HZ 1000000

class IrController {
private:
    static constexpr const char* TAG = "IrController";
    rmt_channel_handle_t tx_channel_ = nullptr;
    rmt_encoder_handle_t nec_encoder_ = nullptr;
    rmt_encoder_handle_t copy_encoder_ = nullptr;

    void SendNec(uint16_t address, uint16_t command, int repeat_count) {
        ir_nec_scan_code_t scan_code = {
            .address = address,
            .command = command,
        };
        rmt_transmit_config_t transmit_config = {
            .loop_count = 0,
            .flags = {
                .eot_level = 0,
            },
        };

        for (int i = 0; i < repeat_count; i++) {
            ESP_ERROR_CHECK(rmt_transmit(tx_channel_, nec_encoder_, &scan_code, sizeof(scan_code), &transmit_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(tx_channel_, 1000));
            if (i + 1 < repeat_count) {
                vTaskDelay(pdMS_TO_TICKS(110));
            }
        }
        ESP_LOGI(TAG, "Sent NEC address=0x%04X command=0x%04X repeat=%d", address, command, repeat_count);
    }

    void SendDog(dog_ir_cmd_t cmd) {
        const dog_ir_code_entry_t* entry = dog_ir_get_code(cmd);
        if (!entry || entry->symbol_count == 0 || !entry->symbols) {
            throw std::runtime_error(std::string("Dog IR code not loaded: ") + (entry ? entry->name : "?"));
        }
        // Export symbols are TX-ready — burst ~1s (same as test_ir learn verify).
        esp_err_t err = ir_raw_tx_send_burst(tx_channel_, copy_encoder_, entry->symbols, entry->symbol_count, 1000);
        if (err != ESP_OK) {
            throw std::runtime_error(std::string("IR transmit failed: ") + entry->name);
        }
        ESP_LOGI(TAG, "Sent dog IR %s fp=0x%08lX (%u sym)",
                 entry->name, (unsigned long)entry->fingerprint, (unsigned)entry->symbol_count);
    }

    void SendR839(r839_ir_cmd_t cmd) {
        const r839_ir_code_entry_t* entry = r839_ir_get_code(cmd);
        if (!entry || entry->symbol_count == 0 || !entry->symbols) {
            throw std::runtime_error(std::string("R839 IR code not loaded: ") + (entry ? entry->name : "?"));
        }
        // Export symbols are TX-ready — burst ~1s (same as robot_839_ir learn verify).
        esp_err_t err = ir_raw_tx_send_burst(tx_channel_, copy_encoder_, entry->symbols, entry->symbol_count, 1000);
        if (err != ESP_OK) {
            throw std::runtime_error(std::string("IR transmit failed: ") + entry->name);
        }
        ESP_LOGI(TAG, "Sent r839 IR %s fp=0x%08lX (%u sym)",
                 entry->name, (unsigned long)entry->fingerprint, (unsigned)entry->symbol_count);
    }

    void Initialize() {
        ir_robot_type_load();
        ESP_ERROR_CHECK(ir_raw_tx_init(&tx_channel_, &copy_encoder_, IR_TX_GPIO));

        ir_nec_encoder_config_t nec_encoder_cfg = {
            .resolution = IR_RESOLUTION_HZ,
        };
        ESP_ERROR_CHECK(rmt_new_ir_nec_encoder(&nec_encoder_cfg, &nec_encoder_));
        dog_ir_codes_init();
        r839_ir_codes_init();
    }

    /* Lenh di chuyen chung cho moi loai robot — dispatch theo robot dang active. */
    enum class Move {
        kForward,
        kBackward,
        kLeft,
        kRight,
        kStop,
    };

    void SendActiveMove(Move move) {
        const ir_robot_type_t robot = ir_robot_type_get();

        switch (robot) {
        case IR_ROBOT_DOG:
            switch (move) {
            case Move::kForward:  SendDog(DOG_IR_FORWARD); return;
            case Move::kBackward: SendDog(DOG_IR_BACK);    return;
            case Move::kLeft:     SendDog(DOG_IR_LEFT);    return;
            case Move::kRight:    SendDog(DOG_IR_RIGHT);   return;
            case Move::kStop:     SendDog(DOG_IR_STOP);    return;
            }
            break;
        case IR_ROBOT_R839:
            switch (move) {
            case Move::kForward:  SendR839(R839_IR_FORWARD);    return;
            case Move::kBackward: SendR839(R839_IR_BACKWARD);   return;
            case Move::kLeft:     SendR839(R839_IR_TURN_LEFT);  return;
            case Move::kRight:    SendR839(R839_IR_TURN_RIGHT); return;
            case Move::kStop:     SendR839(R839_IR_STOP);       return;
            }
            break;
        default:
            break;
        }
        throw std::runtime_error("Khong co lenh IR cho robot dang active");
    }

    void RegisterRobotTypeTools(McpServer& mcp_server) {
        mcp_server.AddTool(
            "self.ir.get_robot_type",
            "Cho biet robot IR dang duoc dieu khien: dog | r839.",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                return std::string(ir_robot_type_name(ir_robot_type_get()));
            });

        mcp_server.AddTool(
            "self.ir.set_robot_type",
            "Chon loai robot IR se dieu khien va luu vao NVS. type: dog | r839. "
            "Sau khi doi, dung nhom self.robot.* de dieu khien ngay (khong can khoi dong lai).",
            PropertyList({
                Property("type", kPropertyTypeString),
            }),
            [](const PropertyList& properties) -> ReturnValue {
                std::string name = properties["type"].value<std::string>();
                ir_robot_type_t type;
                if (!ir_robot_type_parse(name.c_str(), &type)) {
                    throw std::runtime_error("Loai robot khong hop le (dung dog hoac r839)");
                }
                if (!ir_robot_type_set(type)) {
                    throw std::runtime_error("Khong luu duoc loai robot");
                }
                return std::string(ir_robot_type_name(type));
            });
    }

    void RegisterCommonMoveTools(McpServer& mcp_server) {
        auto send_move = [this](Move move) -> ReturnValue {
            SendActiveMove(move);
            return true;
        };

        mcp_server.AddTool("self.robot.forward",
            "UU TIEN DUNG LENH NAY. Robot dang chon (dog/r839) TIEN VE PHIA TRUOC.",
            PropertyList(), [send_move](const PropertyList&) { return send_move(Move::kForward); });
        mcp_server.AddTool("self.robot.backward",
            "UU TIEN DUNG LENH NAY. Robot dang chon LUI VE PHIA SAU.",
            PropertyList(), [send_move](const PropertyList&) { return send_move(Move::kBackward); });
        mcp_server.AddTool("self.robot.turn_left",
            "UU TIEN DUNG LENH NAY. Robot dang chon RE TRAI / quay trai.",
            PropertyList(), [send_move](const PropertyList&) { return send_move(Move::kLeft); });
        mcp_server.AddTool("self.robot.turn_right",
            "UU TIEN DUNG LENH NAY. Robot dang chon RE PHAI / quay phai.",
            PropertyList(), [send_move](const PropertyList&) { return send_move(Move::kRight); });
        mcp_server.AddTool("self.robot.stop",
            "UU TIEN DUNG LENH NAY. Robot dang chon DUNG LAI.",
            PropertyList(), [send_move](const PropertyList&) { return send_move(Move::kStop); });
    }

    void RegisterDogIrTools(McpServer& mcp_server) {
        auto send_dog = [this](dog_ir_cmd_t cmd) -> ReturnValue {
            SendDog(cmd);
            return true;
        };

        /* Lenh di chuyen cua dog da co trong self.robot.* — khong dang ky lai de tiet kiem
           so tool (server chi nhan toi da 32 tool). O day chi con dong tac rieng cua dog. */
        mcp_server.AddTool("self.ir.dog.hello",
            "Robot cho IR: CHAO / say hello.",
            PropertyList(), [send_dog](const PropertyList&) { return send_dog(DOG_IR_HELLO); });
        mcp_server.AddTool("self.ir.dog.tease",
            "Robot cho IR: TREU CHOC / amuse puppy.",
            PropertyList(), [send_dog](const PropertyList&) { return send_dog(DOG_IR_TEASE); });
        mcp_server.AddTool("self.ir.dog.creep",
            "Robot cho IR: BO TOI (creep forward).",
            PropertyList(), [send_dog](const PropertyList&) { return send_dog(DOG_IR_CREEP); });
        mcp_server.AddTool("self.ir.dog.coquetry",
            "Robot cho IR: LAM DIEU (coquetry).",
            PropertyList(), [send_dog](const PropertyList&) { return send_dog(DOG_IR_COQUETRY); });
        mcp_server.AddTool("self.ir.dog.pushup",
            "Robot cho IR: HIT DAT (push up).",
            PropertyList(), [send_dog](const PropertyList&) { return send_dog(DOG_IR_PUSHUP); });
        mcp_server.AddTool("self.ir.dog.lie_down",
            "Robot cho IR: NAM XUONG (get down).",
            PropertyList(), [send_dog](const PropertyList&) { return send_dog(DOG_IR_LIE_DOWN); });
        mcp_server.AddTool("self.ir.dog.sit",
            "Robot cho IR: NGOI XUONG (sit down).",
            PropertyList(), [send_dog](const PropertyList&) { return send_dog(DOG_IR_SIT); });
    }

public:
    IrController() {
        Initialize();

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.ir.send",
            "Send raw NEC infrared code (TX only).",
            PropertyList({
                Property("address", kPropertyTypeInteger, 0, 65535),
                Property("command", kPropertyTypeInteger, 0, 65535),
                Property("repeat", kPropertyTypeInteger, 1, 1, 6),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                uint16_t address = properties["address"].value<int>();
                uint16_t command = properties["command"].value<int>();
                int repeat = properties["repeat"].value<int>();
                SendNec(address, command, repeat);
                return true;
            });

        RegisterRobotTypeTools(mcp_server);

        /* Lenh di chuyen chung — luon theo robot dang active, doi robot la dung duoc ngay. */
        RegisterCommonMoveTools(mcp_server);

        /* Dong tac rieng chi robot dog moi co. */
        RegisterDogIrTools(mcp_server);
    }
};

#endif // __IR_CONTROLLER_H__
