#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/rmt_tx.h"
#include "ir_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    R839_IR_FORWARD = 0,
    R839_IR_BACKWARD,
    R839_IR_TURN_LEFT,
    R839_IR_TURN_RIGHT,
    R839_IR_STOP,
    R839_IR_COUNT
} r839_ir_cmd_t;

typedef struct {
    r839_ir_cmd_t cmd;
    const char *name;
    uint32_t fingerprint;
    const rmt_symbol_word_t *symbols;
    size_t symbol_count;
} r839_ir_code_entry_t;

void r839_ir_codes_init(void);
const r839_ir_code_entry_t *r839_ir_get_code(r839_ir_cmd_t cmd);

#ifdef __cplusplus
}
#endif
