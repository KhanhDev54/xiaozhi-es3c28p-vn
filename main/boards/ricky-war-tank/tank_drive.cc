#include "tank_drive.h"

#include <esp_log.h>

#include "config.h"

static const char* TAG = "TankDrive";

void TankDrive::Init() {
    motor_a_.Init(MOTOR_A_IN1_GPIO, MOTOR_A_IN2_GPIO, LEDC_CHANNEL_0, LEDC_CHANNEL_1);
    motor_b_.Init(MOTOR_B_IN1_GPIO, MOTOR_B_IN2_GPIO, LEDC_CHANNEL_2, LEDC_CHANNEL_3);
    inited_ = true;
    ESP_LOGI(TAG, "Tank drive ready (A: %d/%d, B: %d/%d)", (int)MOTOR_A_IN1_GPIO,
             (int)MOTOR_A_IN2_GPIO, (int)MOTOR_B_IN1_GPIO, (int)MOTOR_B_IN2_GPIO);
}

void TankDrive::Stop() {
    if (!inited_) {
        return;
    }
    motor_a_.Stop();
    motor_b_.Stop();
}

void TankDrive::Forward(int speed) {
    if (!inited_) {
        return;
    }
    motor_a_.Forward(speed);
    motor_b_.Forward(speed);
}

void TankDrive::Backward(int speed) {
    if (!inited_) {
        return;
    }
    motor_a_.Backward(speed);
    motor_b_.Backward(speed);
}

void TankDrive::TurnLeft(int speed) {
    if (!inited_) {
        return;
    }
    motor_a_.Stop();
    motor_b_.Forward(speed);
}

void TankDrive::TurnRight(int speed) {
    if (!inited_) {
        return;
    }
    motor_a_.Forward(speed);
    motor_b_.Stop();
}

void TankDrive::SpinLeft(int speed) {
    if (!inited_) {
        return;
    }
    motor_a_.Backward(speed);
    motor_b_.Forward(speed);
}

void TankDrive::SpinRight(int speed) {
    if (!inited_) {
        return;
    }
    motor_a_.Forward(speed);
    motor_b_.Backward(speed);
}
