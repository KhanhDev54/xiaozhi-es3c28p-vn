#include "r839_ir_codes.h"
#include "r839_ir_symbols.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "R839IrCodes";
static const char *NVS_NAMESPACE = "r839_ir";

typedef struct {
    uint32_t fingerprint;
    uint8_t symbol_count;
    rmt_symbol_word_t symbols[IR_MAX_SYMBOLS];
} r839_ir_nvs_blob_t;

static r839_ir_code_entry_t s_runtime[R839_IR_COUNT];
static bool s_initialized = false;

static bool load_from_nvs(r839_ir_cmd_t cmd, r839_ir_code_entry_t *out)
{
    char key[16];
    snprintf(key, sizeof(key), "cmd_%02u", (unsigned)cmd);

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    r839_ir_nvs_blob_t blob = {};
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &len);
    nvs_close(handle);

    if (err != ESP_OK || blob.symbol_count == 0 || blob.symbol_count > IR_MAX_SYMBOLS) {
        return false;
    }
    if (blob.fingerprint != out->fingerprint) {
        ESP_LOGW(TAG, "NVS fp mismatch for %s", out->name);
        return false;
    }

    static rmt_symbol_word_t nvs_storage[R839_IR_COUNT][IR_MAX_SYMBOLS];
    for (size_t i = 0; i < blob.symbol_count; i++) {
        nvs_storage[cmd][i].duration0 = blob.symbols[i].duration0;
        nvs_storage[cmd][i].duration1 = blob.symbols[i].duration1;
        /* NVS may store RX polarity — invert to TX-ready. */
        nvs_storage[cmd][i].level0 = blob.symbols[i].level0 ? 0 : 1;
        nvs_storage[cmd][i].level1 = blob.symbols[i].level1 ? 0 : 1;
    }
    out->symbols = nvs_storage[cmd];
    out->symbol_count = blob.symbol_count;
    return true;
}

void r839_ir_codes_init(void)
{
    if (s_initialized) {
        return;
    }

    size_t baked_count = kR839IrBakedEntryCount;
    if (baked_count > R839_IR_COUNT) {
        baked_count = R839_IR_COUNT;
    }

    for (size_t i = 0; i < baked_count; i++) {
        s_runtime[i].cmd = (r839_ir_cmd_t)i;
        s_runtime[i].name = kR839IrBakedEntries[i].name;
        s_runtime[i].fingerprint = kR839IrBakedEntries[i].fingerprint;
        s_runtime[i].symbols = kR839IrBakedEntries[i].symbols;
        s_runtime[i].symbol_count = kR839IrBakedEntries[i].symbol_count;

        if (s_runtime[i].symbol_count == 0) {
            load_from_nvs((r839_ir_cmd_t)i, &s_runtime[i]);
        }
    }

    int loaded = 0;
    for (int i = 0; i < (int)baked_count; i++) {
        if (s_runtime[i].symbol_count > 0) {
            loaded++;
            ESP_LOGI(TAG, "Ready %s (%u sym) fp=0x%08lX",
                     s_runtime[i].name, (unsigned)s_runtime[i].symbol_count,
                     (unsigned long)s_runtime[i].fingerprint);
        } else {
            ESP_LOGE(TAG, "Missing IR data for %s — run tools/generate_r839_ir_symbols.py",
                     s_runtime[i].name);
        }
    }
    ESP_LOGI(TAG, "R839 IR codes: %d/%d loaded", loaded, (int)baked_count);
    s_initialized = true;
}

const r839_ir_code_entry_t *r839_ir_get_code(r839_ir_cmd_t cmd)
{
    if (!s_initialized) {
        r839_ir_codes_init();
    }
    if (cmd < 0 || cmd >= R839_IR_COUNT) {
        return NULL;
    }
    return &s_runtime[cmd];
}
