#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/rmt_tx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DOG_IR_MAX_SYMBOLS 64

typedef enum {
    DOG_IR_FORWARD = 0,
    DOG_IR_BACK,
    DOG_IR_LEFT,
    DOG_IR_RIGHT,
    DOG_IR_HELLO,
    DOG_IR_TEASE,
    DOG_IR_CREEP,
    DOG_IR_COQUETRY,
    DOG_IR_PUSHUP,
    DOG_IR_LIE_DOWN,
    DOG_IR_SIT,
    DOG_IR_STOP,
    DOG_IR_COUNT
} dog_ir_cmd_t;

typedef struct {
    dog_ir_cmd_t cmd;
    const char *name;
    uint32_t fingerprint;
    const rmt_symbol_word_t *symbols;
    size_t symbol_count;
} dog_ir_code_entry_t;

void dog_ir_codes_init(void);
const dog_ir_code_entry_t *dog_ir_get_code(dog_ir_cmd_t cmd);
const dog_ir_code_entry_t *dog_ir_find_by_fingerprint(uint32_t fingerprint);

#ifdef __cplusplus
}
#endif
