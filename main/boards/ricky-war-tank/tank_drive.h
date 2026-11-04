#ifndef TANK_DRIVE_H_
#define TANK_DRIVE_H_

#include "tank_motor.h"

/** Điều khiển xe tank 2 bánh: Motor A trái, Motor B phải. */
class TankDrive {
public:
    void Init();
    void Stop();
    void Forward(int speed);
    void Backward(int speed);
    void TurnLeft(int speed);
    void TurnRight(int speed);
    void SpinLeft(int speed);
    void SpinRight(int speed);

private:
    TankMotor motor_a_;
    TankMotor motor_b_;
    bool inited_ = false;
};

#endif  // TANK_DRIVE_H_
