#ifndef HUNONIC_MOTOR_H_
#define HUNONIC_MOTOR_H_

#include <cstdint>
#include <driver/gpio.h>
#include <driver/ledc.h>

/** Mot motor DC qua cau H L298: IN1 tien, IN2 lui (PWM LEDC). */
class HunonicMotor {
public:
    HunonicMotor() = default;
    void Init(gpio_num_t in1, gpio_num_t in2, ledc_channel_t ch_in1, ledc_channel_t ch_in2);
    void Stop();
    void Forward(int speed_percent);
    void Backward(int speed_percent);

private:
    gpio_num_t in1_ = GPIO_NUM_NC;
    gpio_num_t in2_ = GPIO_NUM_NC;
    ledc_channel_t ch_in1_ = LEDC_CHANNEL_0;
    ledc_channel_t ch_in2_ = LEDC_CHANNEL_1;
    bool inited_ = false;
};

#endif  // HUNONIC_MOTOR_H_
