#ifndef __WALLE_MOVEMENTS_H__
#define __WALLE_MOVEMENTS_H__

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oscillator.h"

#define FORWARD 1
#define BACKWARD -1
#define LEFT 1
#define RIGHT -1
#define BOTH 0

#define SERVO_LIMIT_DEFAULT 240

#define LEFT_TRACK 0
#define RIGHT_TRACK 1
#define LEFT_ARM 2
#define RIGHT_ARM 3
#define HEAD 4
#define SERVO_COUNT 5

#define TRACK_STOP 90
#define HEAD_CENTER 90
#define LEFT_ARM_DOWN 45
#define RIGHT_ARM_DOWN 135
#define LEFT_ARM_UP 135
#define RIGHT_ARM_UP 45

class WallE {
public:
    WallE();
    ~WallE();

    void Init(int left_track, int right_track, int left_arm, int right_arm, int head);
    void AttachServos();
    void DetachServos();

    void SetTrims(int left_track, int right_track, int left_arm, int right_arm, int head);

    void MoveServos(int time, int servo_target[]);
    void MoveSingle(int position, int servo_number);
    void OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                         double phase_diff[SERVO_COUNT], float cycle = 1);

    void Home();
    bool GetRestState();
    void SetRestState(bool state);

    // 360° tracks: speed 0-90, duration in ms. 90 PWM = stop.
    void Drive(int left_speed, int right_speed, int duration_ms);
    void Forward(int speed, int duration_ms);
    void Backward(int speed, int duration_ms);
    void TurnLeft(int speed, int duration_ms);
    void TurnRight(int speed, int duration_ms);
    void StopTracks();

    // 180° arms (mirrored): dir LEFT / RIGHT / BOTH
    void ArmsUp(int period, int dir = BOTH);
    void ArmsDown(int period, int dir = BOTH);
    void ArmsWave(int steps, int period, int dir = BOTH);

    // 180° head
    void LookLeft(int amount, int period);
    void LookRight(int amount, int period);
    void LookCenter(int period);
    void Nod(int steps, int amount, int period);

    void EnableServoLimit(int speed_limit_degree_per_sec = SERVO_LIMIT_DEFAULT);
    void DisableServoLimit();

private:
    Oscillator servo_[SERVO_COUNT];
    int servo_pins_[SERVO_COUNT];
    int servo_trim_[SERVO_COUNT];

    unsigned long final_time_;
    float increment_[SERVO_COUNT];

    bool is_resting_;

    void FillPose(int positions[SERVO_COUNT]);
    int Clamp(int value, int min_v, int max_v);
    void Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                 double phase_diff[SERVO_COUNT], float steps);
};

#endif  // __WALLE_MOVEMENTS_H__
