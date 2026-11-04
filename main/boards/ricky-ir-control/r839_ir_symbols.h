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
} r839_ir_baked_entry_t;

extern const r839_ir_baked_entry_t kR839IrBakedEntries[];
extern const size_t kR839IrBakedEntryCount;

#ifdef __cplusplus
}
#endif
