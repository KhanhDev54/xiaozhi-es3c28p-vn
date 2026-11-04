#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/rmt_tx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uint32_t fingerprint;
    const rmt_symbol_word_t *symbols;
    size_t symbol_count;
} dog_ir_baked_entry_t;

extern const dog_ir_baked_entry_t kDogIrBakedEntries[];
extern const size_t kDogIrBakedEntryCount;

#ifdef __cplusplus
}
#endif
