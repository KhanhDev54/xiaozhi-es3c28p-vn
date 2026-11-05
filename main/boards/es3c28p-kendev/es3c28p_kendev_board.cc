// KenDev firmware for the LCD Wiki ES3C28P (ESP32-S3 2.8" touch).
// Hardware init follows the upstream board quandong/esp32-s3-spi-2.8; adds the
// FT6336 touch panel (as an LVGL input device), Vietnamese features and the launcher UI.

#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "esp_lcd_ili9341.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/circular_strip.h"

#include "alarm_manager.h"
#include "ft6336_touch.h"
#include "info_services.h"
#include "launcher.h"
#include "radio_player.h"
#include "touch_cal.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_touch.h>
#include <esp_lvgl_port.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>

#define TAG "ES3C28P"

// ILI9341V vendor init sequence (from the upstream board)
static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
    {0xCF, (uint8_t []){0x00, 0xC1, 0x30}, 3, 0},
    {0xED, (uint8_t []){0x64, 0x03, 0x12, 0x81}, 4, 0},
    {0xE8, (uint8_t []){0x85, 0x00, 0x78}, 3, 0},
    {0xCB, (uint8_t []){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, (uint8_t []){0x20}, 1, 0},
    {0xEA, (uint8_t []){0x00, 0x00}, 2, 0},
    {0xC0, (uint8_t []){0x13}, 1, 0},
    {0xC1, (uint8_t []){0x13}, 1, 0},
    {0xC5, (uint8_t []){0x22, 0x35}, 2, 0},
    {0xC7, (uint8_t []){0xBD}, 1, 0},
    {0, (uint8_t []){0}, 0x11, 0},
    {0x36, (uint8_t []){0x08}, 1, 0},
    {0xB6, (uint8_t []){0x0A, 0xA2}, 2, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0xF6, (uint8_t []){0x01, 0x30}, 2, 0},
    {0xB1, (uint8_t []){0x00, 0x1B}, 2, 0},
    {0xF2, (uint8_t []){0x00}, 1, 0},
    {0x26, (uint8_t []){0x01}, 1, 0},
    {0xE0, (uint8_t []){0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00}, 15, 0},
    {0xE1, (uint8_t []){0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F}, 15, 0},
    {0, (uint8_t []){0}, 0x11, 0},
    {0, (uint8_t []){0}, 0x29, 0},
};

class Es3c28pKendevBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // GPIO1 low enables the on-board amplifier
    void InitializeAudioPaEnable() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << AUDIO_PA_ENABLE_PIN);
        io_conf.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io_conf);
        gpio_set_level(AUDIO_PA_ENABLE_PIN, 0);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeIli9341Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        const ili9341_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
        };
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.flags.reset_active_high = 1;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void *)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouch() {
        // Manual reset (RST low 10 ms, then wait for the controller to boot)
        gpio_config_t rst = {};
        rst.pin_bit_mask = 1ULL << TOUCH_RST_PIN;
        rst.mode = GPIO_MODE_OUTPUT;
        gpio_config(&rst);
        gpio_set_level(TOUCH_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(TOUCH_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));

        esp_lcd_panel_io_handle_t tp_io = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {};
        tp_io_config.dev_addr = TOUCH_I2C_ADDR;
        tp_io_config.control_phase_bytes = 1;
        tp_io_config.lcd_cmd_bits = 8;
        tp_io_config.lcd_param_bits = 8;
        tp_io_config.flags.disable_control_phase = 1;
        tp_io_config.scl_speed_hz = 400 * 1000;
        if (esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io) != ESP_OK) {
            ESP_LOGE(TAG, "Touch panel IO failed");
            return;
        }
        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = 4095;  // raw values; mapped in touch_cal::Process
        tp_cfg.y_max = 4095;
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;
        tp_cfg.int_gpio_num = GPIO_NUM_NC;
        tp_cfg.process_coordinates = touch_cal::Process;
        uint8_t vend = 0, chip = 0;
        if (ft6336_touch_new(tp_io, &tp_cfg, &touch_, &vend, &chip) != ESP_OK) {
            ESP_LOGE(TAG, "FT6336 not found");
            touch_ = nullptr;
            return;
        }
        ESP_LOGI(TAG, "Touch FT6336 vendor 0x%02X chip 0x%02X", vend, chip);
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = touch_,
        };
        lvgl_port_lock(0);
        lvgl_port_add_touch(&touch_cfg);
        lvgl_port_unlock();
    }

    void InitializeTools() {
        // Báo thức, hẹn giờ, âm lịch, chế độ ban đêm
        AlarmManager::GetInstance().Start(true);
        // Thời tiết, tin tức, giá vàng SJC, tỷ giá Vietcombank
        InfoServices::GetInstance().RegisterTools();
        // Radio VOV
        RadioPlayer::GetInstance().RegisterTools();
    }

public:
    Es3c28pKendevBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeAudioPaEnable();
        InitializeIli9341Display();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
        Launcher::GetInstance().Start(display_, GetBacklight());
    }

    virtual Led* GetLed() override {
        static CircularStrip led(BUILTIN_LED_GPIO, 1);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            i2c_bus_,
            I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            true,
            true);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(Es3c28pKendevBoard);
