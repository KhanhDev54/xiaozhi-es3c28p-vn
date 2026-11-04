#include "dog_ir_codes.h"
#include "dog_ir_symbols.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "DogIrCodes";
static const char *NVS_NAMESPACE = "dog_ir";

typedef struct {
    uint32_t fingerprint;
    uint8_t symbol_count;
    rmt_symbol_word_t symbols[DOG_IR_MAX_SYMBOLS];
} dog_ir_nvs_blob_t;

static dog_ir_code_entry_t s_runtime[DOG_IR_COUNT];
static bool s_initialized = false;

static bool load_from_nvs(dog_ir_cmd_t cmd, dog_ir_code_entry_t *out)
{
    char key[16];
    snprintf(key, sizeof(key), "cmd_%02u", (unsigned)cmd);

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    dog_ir_nvs_blob_t blob = {};
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(handle, key, &blob, &len);
    nvs_close(handle);

    if (err != ESP_OK || blob.symbol_count == 0 || blob.symbol_count > DOG_IR_MAX_SYMBOLS) {
        return false;
    }
    if (blob.fingerprint != out->fingerprint) {
        ESP_LOGW(TAG, "NVS fp mismatch for %s", out->name);
        return false;
    }

    static rmt_symbol_word_t nvs_storage[DOG_IR_COUNT][DOG_IR_MAX_SYMBOLS];
    memcpy(nvs_storage[cmd], blob.symbols, blob.symbol_count * sizeof(rmt_symbol_word_t));
    out->symbols = nvs_storage[cmd];
    out->symbol_count = blob.symbol_count;
    return true;
}

void dog_ir_codes_init(void)
{
    if (s_initialized) {
        return;
    }

    for (size_t i = 0; i < DOG_IR_COUNT; i++) {
        s_runtime[i].cmd = (dog_ir_cmd_t)i;
        s_runtime[i].name = kDogIrBakedEntries[i].name;
        s_runtime[i].fingerprint = kDogIrBakedEntries[i].fingerprint;
        s_runtime[i].symbols = kDogIrBakedEntries[i].symbols;
        s_runtime[i].symbol_count = kDogIrBakedEntries[i].symbol_count;

        if (s_runtime[i].symbol_count == 0) {
            load_from_nvs((dog_ir_cmd_t)i, &s_runtime[i]);
        }
    }

    int loaded = 0;
    for (int i = 0; i < DOG_IR_COUNT; i++) {
        if (s_runtime[i].symbol_count > 0) {
            loaded++;
            ESP_LOGI(TAG, "Ready %s (%u sym) fp=0x%08lX",
                     s_runtime[i].name, (unsigned)s_runtime[i].symbol_count,
                     (unsigned long)s_runtime[i].fingerprint);
        } else {
            ESP_LOGE(TAG, "Missing IR data for %s — run tools/generate_dog_ir_symbols.py",
                     s_runtime[i].name);
        }
    }
    ESP_LOGI(TAG, "Dog IR codes: %d/%d loaded", loaded, DOG_IR_COUNT);
    s_initialized = true;
}

const dog_ir_code_entry_t *dog_ir_get_code(dog_ir_cmd_t cmd)
{
    if (!s_initialized) {
        dog_ir_codes_init();
    }
    if (cmd < 0 || cmd >= DOG_IR_COUNT) {
        return NULL;
    }
    return &s_runtime[cmd];
}

const dog_ir_code_entry_t *dog_ir_find_by_fingerprint(uint32_t fingerprint)
{
    if (!s_initialized) {
        dog_ir_codes_init();
    }
    for (int i = 0; i < DOG_IR_COUNT; i++) {
        if (s_runtime[i].fingerprint == fingerprint) {
            return &s_runtime[i];
        }
    }
    return NULL;
}
