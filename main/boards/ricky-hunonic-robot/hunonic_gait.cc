#include "hunonic_gait.h"

#include <esp_log.h>

#include "config.h"

static const char* TAG = "HunonicGait";

void HunonicGait::Init() {
    left_.Init(MOTOR_LEFT_IN1_GPIO, MOTOR_LEFT_IN2_GPIO, LEDC_CHANNEL_0, LEDC_CHANNEL_1);
    right_.Init(MOTOR_RIGHT_IN1_GPIO, MOTOR_RIGHT_IN2_GPIO, LEDC_CHANNEL_2, LEDC_CHANNEL_3);
    inited_ = true;
    ESP_LOGI(TAG, "Gait ready (L: %d/%d, R: %d/%d)", (int)MOTOR_LEFT_IN1_GPIO,
             (int)MOTOR_LEFT_IN2_GPIO, (int)MOTOR_RIGHT_IN1_GPIO, (int)MOTOR_RIGHT_IN2_GPIO);
}

void HunonicGait::Stop() {
    if (!inited_) {
        return;
    }
    left_.Stop();
    right_.Stop();
}

void HunonicGait::WalkHalf(HunonicDir dir, bool right_foot, int speed) {
    if (!inited_) {
        return;
    }
    left_.Stop();
    right_.Stop();

    HunonicMotor& foot = right_foot ? right_ : left_;
    switch (dir) {
        case HunonicDir::Forward:
            foot.Forward(speed);
            break;
        case HunonicDir::Backward:
            foot.Backward(speed);
            break;
        case HunonicDir::Left:
            if (right_foot) {
                right_.Backward(speed);
            } else {
                left_.Forward(speed);
            }
            break;
        case HunonicDir::Right:
            if (right_foot) {
                right_.Forward(speed);
            } else {
                left_.Backward(speed);
            }
            break;
    }
}

void HunonicGait::Slide(HunonicDir dir, int speed) {
    if (!inited_) {
        return;
    }
    switch (dir) {
        case HunonicDir::Forward:
            left_.Forward(speed);
            right_.Forward(speed);
            break;
        case HunonicDir::Backward:
            left_.Backward(speed);
            right_.Backward(speed);
            break;
        case HunonicDir::Left:
            left_.Forward(speed);
            right_.Backward(speed);
            break;
        case HunonicDir::Right:
            left_.Backward(speed);
            right_.Forward(speed);
            break;
    }
}
