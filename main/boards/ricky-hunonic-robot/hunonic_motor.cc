#include "hunonic_motor.h"

#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>

#include "config.h"

static const char* TAG = "HunonicMotor";

#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define LEDC_MAX_DUTY ((1 << 10) - 1)
#define LEDC_FREQ_HZ 2000

static bool s_timer_inited = false;

static inline int ClampSpeed(int speed) {
    if (speed <= 0) {
        return 0;
    }
    if (speed < HUNONIC_SPEED_MIN_EFFECTIVE) {
        return 0;
    }
    if (speed > 100) {
        return 100;
    }
    return speed;
}

static inline uint32_t SpeedToDuty(int speed) {
    const int s = ClampSpeed(speed);
    if (s == 0) {
        return 0;
    }
    uint32_t duty = (uint32_t)((s * LEDC_MAX_DUTY) / 100);
    return duty ? duty : 1u;
}

void HunonicMotor::Init(gpio_num_t in1, gpio_num_t in2, ledc_channel_t ch_in1, ledc_channel_t ch_in2) {
    in1_ = in1;
    in2_ = in2;
    ch_in1_ = ch_in1;
    ch_in2_ = ch_in2;

    if (!s_timer_inited) {
        ledc_timer_config_t timer = {
            .speed_mode = LEDC_MODE,
            .duty_resolution = LEDC_DUTY_RES,
            .timer_num = LEDC_TIMER,
            .freq_hz = LEDC_FREQ_HZ,
            .clk_cfg = LEDC_AUTO_CLK,
            .deconfigure = false,
        };
        ESP_ERROR_CHECK(ledc_timer_config(&timer));
        s_timer_inited = true;
    }

    ledc_channel_config_t ch1 = {
        .gpio_num = in1_,
        .speed_mode = LEDC_MODE,
        .channel = ch_in1_,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {.output_invert = 0},
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch1));

    ledc_channel_config_t ch2 = {
        .gpio_num = in2_,
        .speed_mode = LEDC_MODE,
        .channel = ch_in2_,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {.output_invert = 0},
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch2));

    inited_ = true;
    ESP_LOGI(TAG, "LEDC %d Hz, IN1=%d IN2=%d ch=%d/%d", LEDC_FREQ_HZ, (int)in1_, (int)in2_,
             (int)ch_in1_, (int)ch_in2_);
}

void HunonicMotor::Stop() {
    if (!inited_) {
        return;
    }
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ch_in1_, 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ch_in1_));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ch_in2_, 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ch_in2_));
}

void HunonicMotor::Forward(int speed_percent) {
    if (!inited_) {
        return;
    }
    const uint32_t d = SpeedToDuty(speed_percent);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ch_in2_, 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ch_in2_));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ch_in1_, d));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ch_in1_));
}

void HunonicMotor::Backward(int speed_percent) {
    if (!inited_) {
        return;
    }
    const uint32_t d = SpeedToDuty(speed_percent);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ch_in1_, 0));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ch_in1_));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ch_in2_, d));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ch_in2_));
}
