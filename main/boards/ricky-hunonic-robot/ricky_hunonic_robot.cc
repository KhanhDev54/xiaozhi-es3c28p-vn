#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_station.h>

#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display.h"
#include "hunonic_controller.h"
#include "mach_tim_oled_display.h"
#include "system_reset.h"
#include "websocket_control_server.h"
#include "wifi_board.h"

#define TAG "RickyHunonic"

class RickyHunonicRobot : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    Display* display_;
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
        display_ = new MachTimOledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                          DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
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
        if (!ws_control_server_->Start(HUNONIC_WEB_CONTROL_PORT)) {
            ESP_LOGE(TAG, "Failed to start WebSocket control server");
            delete ws_control_server_;
            ws_control_server_ = nullptr;
        } else {
            ESP_LOGI(TAG, "Hunonic web control UI started on http://<ip>:%d",
                     HUNONIC_WEB_CONTROL_PORT);
        }
    }

    void StartNetwork() override {
        WifiBoard::StartNetwork();
        vTaskDelay(pdMS_TO_TICKS(1000));
        InitializeHunonicController();
        InitializeWebSocketControlServer();
    }

public:
    RickyHunonicRobot() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplayI2c();
        InitializeSsd1306Display();
        InitializeButtons();
        ws_control_server_ = nullptr;
        ESP_LOGI(TAG, "Ricky Hunonic Robot board init, fw %s", RICKY_HUNONIC_ROBOT_VERSION);
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

DECLARE_BOARD(RickyHunonicRobot);
