#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ESP32-S3 Zero (4MB Flash / 2MB PSRAM) + OLED I2C + INMP441 + MAX98357A
// Pin map follows the "ChatBot AI (Xiaozhi) mod by tandev" wiring diagram.

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// INMP441 and MAX98357A use separate I2S buses (simplex)
#define AUDIO_I2S_METHOD_SIMPLEX

// INMP441 microphone (L/R -> GND)
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_2
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_3
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_4

// MAX98357A amplifier
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_5
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_7

// On-board WS2812 RGB LED of the ESP32-S3 Zero (status LED).
// Set to GPIO_NUM_NC if your board has no RGB LED on this pin.
#define BUILTIN_LED_GPIO        GPIO_NUM_21

// Buttons (wired between GPIO and GND)
#define BOOT_BUTTON_GPIO        GPIO_NUM_1   // Wake up / toggle chat
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_10
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_11

// OLED (I2C, address 0x3C)
#define DISPLAY_SDA_PIN GPIO_NUM_8
#define DISPLAY_SCL_PIN GPIO_NUM_9
#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "OLED display type is not selected"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// External LED ("Đèn") on GPIO12, controllable by voice via MCP lamp tool
#define LAMP_GPIO GPIO_NUM_12

#endif // _BOARD_CONFIG_H_
