#include "walle_movements.h"

#include <algorithm>
#include <cmath>

#include "freertos/idf_additions.h"
#include "oscillator.h"

static const char* TAG = "WallEMovements";

WallE::WallE() {
    is_resting_ = false;
    for (int i = 0; i < SERVO_COUNT; i++) {
        servo_pins_[i] = -1;
        servo_trim_[i] = 0;
    }
}

WallE::~WallE() {
    DetachServos();
}

unsigned long IRAM_ATTR millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

int WallE::Clamp(int value, int min_v, int max_v) {
    return std::max(min_v, std::min(max_v, value));
}

void WallE::Init(int left_track, int right_track, int left_arm, int right_arm, int head) {
    servo_pins_[LEFT_TRACK] = left_track;
    servo_pins_[RIGHT_TRACK] = right_track;
    servo_pins_[LEFT_ARM] = left_arm;
    servo_pins_[RIGHT_ARM] = right_arm;
    servo_pins_[HEAD] = head;

    AttachServos();
    is_resting_ = false;
}

void WallE::AttachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Attach(servo_pins_[i]);
        }
    }
}

void WallE::DetachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].Detach();
        }
    }
}

void WallE::SetTrims(int left_track, int right_track, int left_arm, int right_arm, int head) {
    servo_trim_[LEFT_TRACK] = left_track;
    servo_trim_[RIGHT_TRACK] = right_track;
    servo_trim_[LEFT_ARM] = left_arm;
    servo_trim_[RIGHT_ARM] = right_arm;
    servo_trim_[HEAD] = head;

    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetTrim(servo_trim_[i]);
        }
    }
}

void WallE::FillPose(int positions[SERVO_COUNT]) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            positions[i] = servo_[i].GetPosition();
        } else {
            positions[i] = 90;
        }
    }
    positions[LEFT_TRACK] = TRACK_STOP;
    positions[RIGHT_TRACK] = TRACK_STOP;
}

void WallE::MoveServos(int time, int servo_target[]) {
    if (GetRestState()) {
        SetRestState(false);
    }

    final_time_ = millis() + time;
    if (time > 10) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                increment_[i] = (servo_target[i] - servo_[i].GetPosition()) / (time / 10.0);
            }
        }

        while (millis() < final_time_) {
            for (int i = 0; i < SERVO_COUNT; i++) {
                if (servo_pins_[i] != -1) {
                    servo_[i].SetPosition(servo_[i].GetPosition() + increment_[i]);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                servo_[i].SetPosition(servo_target[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(std::max(time, 1)));
    }

    bool f = true;
    int adjustment_count = 0;
    while (f && adjustment_count < 10) {
        f = false;
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1 && servo_target[i] != servo_[i].GetPosition()) {
                f = true;
                break;
            }
        }
        if (f) {
            for (int i = 0; i < SERVO_COUNT; i++) {
                if (servo_pins_[i] != -1) {
                    servo_[i].SetPosition(servo_target[i]);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            adjustment_count++;
        }
    }
}

void WallE::MoveSingle(int position, int servo_number) {
    position = Clamp(position, 0, 180);
    if (GetRestState()) {
        SetRestState(false);
    }
    if (servo_number >= 0 && servo_number < SERVO_COUNT && servo_pins_[servo_number] != -1) {
        servo_[servo_number].SetPosition(position);
    }
}

void WallE::OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                            double phase_diff[SERVO_COUNT], float cycle) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetO(offset[i]);
            servo_[i].SetA(amplitude[i]);
            servo_[i].SetT(period);
            servo_[i].SetPh(phase_diff[i]);
        }
    }

    double ref = millis();
    double end_time = period * cycle + ref;
    while (millis() < end_time) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                servo_[i].Refresh();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

void WallE::Execute(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period,
                    double phase_diff[SERVO_COUNT], float steps) {
    if (GetRestState()) {
        SetRestState(false);
    }

    int cycles = (int)steps;
    if (cycles >= 1) {
        for (int i = 0; i < cycles; i++) {
            OscillateServos(amplitude, offset, period, phase_diff);
        }
    }
    OscillateServos(amplitude, offset, period, phase_diff, (float)steps - cycles);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void WallE::Home() {
    StopTracks();
    int homes[SERVO_COUNT];
    homes[LEFT_TRACK] = TRACK_STOP;
    homes[RIGHT_TRACK] = TRACK_STOP;
    homes[LEFT_ARM] = LEFT_ARM_DOWN;
    homes[RIGHT_ARM] = RIGHT_ARM_DOWN;
    homes[HEAD] = HEAD_CENTER;
    MoveServos(500, homes);
    is_resting_ = true;
}

bool WallE::GetRestState() {
    return is_resting_;
}

void WallE::SetRestState(bool state) {
    is_resting_ = state;
}

void WallE::Drive(int left_speed, int right_speed, int duration_ms) {
    if (GetRestState()) {
        SetRestState(false);
    }

    left_speed = Clamp(left_speed, -90, 90);
    right_speed = Clamp(right_speed, -90, 90);
    duration_ms = Clamp(duration_ms, 0, 8000);

    // Right track is mechanically mirrored on the Otto header layout.
    servo_[LEFT_TRACK].SetPosition(TRACK_STOP + left_speed);
    servo_[RIGHT_TRACK].SetPosition(TRACK_STOP - right_speed);

    if (duration_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        StopTracks();
    }
}

void WallE::Forward(int speed, int duration_ms) {
    speed = Clamp(std::abs(speed), 10, 90);
    Drive(speed, speed, duration_ms);
}

void WallE::Backward(int speed, int duration_ms) {
    speed = Clamp(std::abs(speed), 10, 90);
    Drive(-speed, -speed, duration_ms);
}

void WallE::TurnLeft(int speed, int duration_ms) {
    speed = Clamp(std::abs(speed), 10, 90);
    Drive(-speed, speed, duration_ms);
}

void WallE::TurnRight(int speed, int duration_ms) {
    speed = Clamp(std::abs(speed), 10, 90);
    Drive(speed, -speed, duration_ms);
}

void WallE::StopTracks() {
    servo_[LEFT_TRACK].SetPosition(TRACK_STOP);
    servo_[RIGHT_TRACK].SetPosition(TRACK_STOP);
}

void WallE::ArmsUp(int period, int dir) {
    period = Clamp(period, 100, 3000);
    int pos[SERVO_COUNT];
    FillPose(pos);
    if (dir == LEFT || dir == BOTH) {
        pos[LEFT_ARM] = LEFT_ARM_UP;
    }
    if (dir == RIGHT || dir == BOTH) {
        pos[RIGHT_ARM] = RIGHT_ARM_UP;
    }
    MoveServos(period, pos);
}

void WallE::ArmsDown(int period, int dir) {
    period = Clamp(period, 100, 3000);
    int pos[SERVO_COUNT];
    FillPose(pos);
    if (dir == LEFT || dir == BOTH) {
        pos[LEFT_ARM] = LEFT_ARM_DOWN;
    }
    if (dir == RIGHT || dir == BOTH) {
        pos[RIGHT_ARM] = RIGHT_ARM_DOWN;
    }
    MoveServos(period, pos);
}

void WallE::ArmsWave(int steps, int period, int dir) {
    steps = Clamp(steps, 1, 10);
    period = Clamp(period, 200, 2000);
    int pos[SERVO_COUNT];
    FillPose(pos);

    const int left_mid = (LEFT_ARM_DOWN + LEFT_ARM_UP) / 2;
    const int right_mid = (RIGHT_ARM_DOWN + RIGHT_ARM_UP) / 2;
    if (dir == LEFT || dir == BOTH) {
        pos[LEFT_ARM] = left_mid;
    }
    if (dir == RIGHT || dir == BOTH) {
        pos[RIGHT_ARM] = right_mid;
    }
    MoveServos(period / 2, pos);

    for (int i = 0; i < steps; i++) {
        if (dir == LEFT || dir == BOTH) {
            pos[LEFT_ARM] = (i % 2 == 0) ? LEFT_ARM_UP : LEFT_ARM_DOWN;
        }
        if (dir == RIGHT || dir == BOTH) {
            pos[RIGHT_ARM] = (i % 2 == 0) ? RIGHT_ARM_UP : RIGHT_ARM_DOWN;
        }
        MoveServos(period / 2, pos);
    }

    ArmsDown(period / 2, dir);
}

void WallE::LookLeft(int amount, int period) {
    amount = Clamp(std::abs(amount), 10, 80);
    period = Clamp(period, 100, 3000);
    int pos[SERVO_COUNT];
    FillPose(pos);
    pos[HEAD] = HEAD_CENTER - amount;
    MoveServos(period, pos);
}

void WallE::LookRight(int amount, int period) {
    amount = Clamp(std::abs(amount), 10, 80);
    period = Clamp(period, 100, 3000);
    int pos[SERVO_COUNT];
    FillPose(pos);
    pos[HEAD] = HEAD_CENTER + amount;
    MoveServos(period, pos);
}

void WallE::LookCenter(int period) {
    period = Clamp(period, 100, 3000);
    int pos[SERVO_COUNT];
    FillPose(pos);
    pos[HEAD] = HEAD_CENTER;
    MoveServos(period, pos);
}

void WallE::Nod(int steps, int amount, int period) {
    steps = Clamp(steps, 1, 8);
    amount = Clamp(std::abs(amount), 10, 40);
    period = Clamp(period, 200, 2000);
    int pos[SERVO_COUNT];
    FillPose(pos);

    for (int i = 0; i < steps; i++) {
        pos[HEAD] = HEAD_CENTER + amount;
        MoveServos(period / 2, pos);
        pos[HEAD] = HEAD_CENTER - amount;
        MoveServos(period / 2, pos);
    }
    pos[HEAD] = HEAD_CENTER;
    MoveServos(period / 2, pos);
}

void WallE::EnableServoLimit(int speed_limit_degree_per_sec) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetLimiter(speed_limit_degree_per_sec);
        }
    }
}

void WallE::DisableServoLimit() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].DisableLimiter();
        }
    }
}
