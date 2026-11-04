#ifndef HUNONIC_GAIT_H_
#define HUNONIC_GAIT_H_

#include <cstdint>

#include "hunonic_motor.h"

enum class HunonicDir : uint8_t {
    Forward = 0,
    Backward,
    Left,
    Right,
};

/** Gait 2 chan: Walk xen ke, Slide dong thoi. */
class HunonicGait {
public:
    void Init();
    void Stop();
    void WalkHalf(HunonicDir dir, bool right_foot, int speed);
    void Slide(HunonicDir dir, int speed);

private:
    HunonicMotor left_;
    HunonicMotor right_;
    bool inited_ = false;
};

#endif  // HUNONIC_GAIT_H_
