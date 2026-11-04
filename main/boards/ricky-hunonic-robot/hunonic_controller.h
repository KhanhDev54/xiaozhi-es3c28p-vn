#ifndef HUNONIC_CONTROLLER_H_
#define HUNONIC_CONTROLLER_H_

void InitializeHunonicController();

struct HunonicWebConfig {
    int speed;
    int step_ms;
    int steps;
    int duration_ms;
    int gap_ms;
};

bool HunonicGetWebConfig(HunonicWebConfig* out);

#endif  // HUNONIC_CONTROLLER_H_
