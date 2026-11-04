#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_station.h>

#include <stdexcept>
#include <string>

#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "mach_tim_oled_display.h"
#include "mcp_server.h"
#include "system_reset.h"
#include "websocket_control_server.h"
#include "wifi_board.h"

#define TAG "RickyWarTank"

extern void InitializeTankController();

class RickyWarTank : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    Display* display_;
    MachTimOledDisplay* eye_display_ = nullptr;
    Button boot_button_;
    WebSocketControlServer* ws_control_server_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    bool TryInitSsd1306(uint8_t dev_addr) {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = dev_addr,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 100 * 1000,
        };

        if (esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_) != ESP_OK) {
            return false;
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        if (esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_) != ESP_OK) {
            esp_lcd_panel_io_del(panel_io_);
            panel_io_ = nullptr;
            return false;
        }

        if (esp_lcd_panel_reset(panel_) != ESP_OK || esp_lcd_panel_init(panel_) != ESP_OK) {
            esp_lcd_panel_del(panel_);
            esp_lcd_panel_io_del(panel_io_);
            panel_ = nullptr;
            panel_io_ = nullptr;
            return false;
        }

        ESP_LOGI(TAG, "SSD1306 OK at I2C addr 0x%02X", dev_addr);
        return true;
    }

    void InitializeSsd1306Display() {
        ESP_LOGI(TAG, "Install SSD1306 driver (SDA=%d SCL=%d)", DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
        if (!TryInitSsd1306(0x3C) && !TryInitSsd1306(0x3D)) {
            ESP_LOGE(TAG, "SSD1306 init failed (tried 0x3C and 0x3D)");
            display_ = new NoDisplay();
            return;
        }

        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));
        eye_display_ = new MachTimOledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                              DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = eye_display_;
    }

    void InitializeEyeStyleTools() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(
            "self.face.get_eye_style",
            "Cho biet bo mat dang dung: otto (mat goc) hoac pika (mat pika tron, de thuong).",
            PropertyList(),
            [](const PropertyList&) -> ReturnValue {
                return std::string(MachTimEyeStyleName(MachTimEyeStyleGet()));
            });

        mcp_server.AddTool(
            "self.face.set_eye_style",
            "Doi bo mat tren man hinh va luu vao NVS. style=otto (mat goc) hoac "
            "pika (mat tron de thuong, co nhay mat va mieng cuoi). Doi hieu luc ngay.",
            PropertyList({
                Property("style", kPropertyTypeString),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string name = properties["style"].value<std::string>();
                MachTimEyeStyle style;
                if (!MachTimEyeStyleParse(name.c_str(), &style)) {
                    throw std::runtime_error("Bo mat khong hop le (dung otto hoac pika)");
                }
                if (!MachTimEyeStyleSet(style)) {
                    throw std::runtime_error("Khong luu duoc bo mat");
                }
                if (eye_display_ != nullptr) {
                    eye_display_->ReloadEyeStyle();
                }
                return std::string(MachTimEyeStyleName(style));
            });

        mcp_server.AddTool(
            "self.screen.get_display_mode",
            "Cho biet che do hien thi: face (khuon mat chiem het man hinh) hoac "
            "wechat (khi noi thi hien text va icon mat nho).",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (eye_display_ == nullptr) {
                    throw std::runtime_error("Man hinh chua san sang");
                }
                return std::string(MachTimFaceLayoutName(eye_display_->GetLayout()));
            });

        mcp_server.AddTool(
            "self.screen.set_display_mode",
            "Doi che do hien thi va luu vao NVS. mode=face (luon hien khuon mat, "
            "khong hien text) hoac wechat (dang noi thi an mat, hien text kem icon "
            "mat nho; luc cho va lang nghe van hien khuon mat).",
            PropertyList({
                Property("mode", kPropertyTypeString),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                if (eye_display_ == nullptr) {
                    throw std::runtime_error("Man hinh chua san sang");
                }
                std::string name = properties["mode"].value<std::string>();
                MachTimFaceLayout layout;
                if (!MachTimFaceLayoutParse(name.c_str(), &layout)) {
                    throw std::runtime_error("Che do khong hop le (dung face hoac wechat)");
                }
                eye_display_->SetLayout(layout);
                return std::string(MachTimFaceLayoutName(layout));
            });
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting &&
                !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    void InitializeWebSocketControlServer() {
        ws_control_server_ = new WebSocketControlServer();
        if (!ws_control_server_->Start(TANK_WEB_CONTROL_PORT)) {
            ESP_LOGE(TAG, "Failed to start HTTP control server");
            delete ws_control_server_;
            ws_control_server_ = nullptr;
        } else {
            ESP_LOGI(TAG, "Tank web control UI started on http://<ip>:%d", TANK_WEB_CONTROL_PORT);
        }
    }

    void StartNetwork() override {
        WifiBoard::StartNetwork();
        vTaskDelay(pdMS_TO_TICKS(1000));
        InitializeTankController();
        InitializeWebSocketControlServer();
    }

public:
    RickyWarTank() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        InitializeEyeStyleTools();
        ws_control_server_ = nullptr;
        ESP_LOGI(TAG, "Ricky War Tank board init, fw %s (eyes: %s)", RICKY_WAR_TANK_VERSION,
                 MachTimEyeStyleName(MachTimEyeStyleGet()));
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                                               AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
                                               AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }
};

DECLARE_BOARD(RickyWarTank);
