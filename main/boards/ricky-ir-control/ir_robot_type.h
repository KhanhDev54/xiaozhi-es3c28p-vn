#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_ROBOT_DOG = 0,
    /* 1 da tung la IR_ROBOT_SONDR (ngung phat trien) — giu trong de gia tri NVS cu khong lech. */
    IR_ROBOT_R839 = 2,
    IR_ROBOT_COUNT
} ir_robot_type_t;

void ir_robot_type_load(void);
ir_robot_type_t ir_robot_type_get(void);
bool ir_robot_type_set(ir_robot_type_t type);
const char *ir_robot_type_name(ir_robot_type_t type);
bool ir_robot_type_parse(const char *name, ir_robot_type_t *out);

#ifdef __cplusplus
}
#endif
