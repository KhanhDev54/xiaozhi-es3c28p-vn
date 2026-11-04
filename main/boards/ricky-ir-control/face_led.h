#pragma once

#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACE_IDLE,
    FACE_LISTENING,
    FACE_SPEAKING,
    FACE_HAPPY,
    FACE_SAD,
    FACE_ANGRY,
    FACE_SURPRISED,
    FACE_THINKING,
    FACE_SLEEP,
    FACE_WINK,
} face_expr_t;

void face_led_init(gpio_num_t gpio, int led_count);
void face_led_set(face_expr_t expr);
void face_led_set_from_emotion(const char *emotion);

#ifdef __cplusplus
}
#endif
