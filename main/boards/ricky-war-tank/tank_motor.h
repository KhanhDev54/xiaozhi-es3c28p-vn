#ifndef TANK_MOTOR_H_
#define TANK_MOTOR_H_

#include <cstdint>
#include <driver/gpio.h>
#include <driver/ledc.h>

/** Một motor DC qua cầu H L298: IN1 tiến, IN2 lùi (PWM LEDC). */
class TankMotor {
public:
    TankMotor() = default;
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

#endif  // TANK_MOTOR_H_
