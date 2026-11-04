#include "ir_robot_type.h"

#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "IrRobotType";
static const char *NVS_NAMESPACE = "ir_ctrl";
static const char *NVS_KEY = "robot_type";

#if defined(CONFIG_IR_ROBOT_DEFAULT_R839)
#define IR_ROBOT_DEFAULT IR_ROBOT_R839
#else
#define IR_ROBOT_DEFAULT IR_ROBOT_DOG
#endif

static ir_robot_type_t s_active = IR_ROBOT_DEFAULT;
static bool s_loaded = false;

static bool is_supported(int32_t value)
{
    return value == IR_ROBOT_DOG || value == IR_ROBOT_R839;
}

const char *ir_robot_type_name(ir_robot_type_t type)
{
    switch (type) {
    case IR_ROBOT_DOG:
        return "dog";
    case IR_ROBOT_R839:
        return "r839";
    default:
        return "unknown";
    }
}

bool ir_robot_type_parse(const char *name, ir_robot_type_t *out)
{
    if (!name || !out) {
        return false;
    }
    if (strcasecmp(name, "dog") == 0) {
        *out = IR_ROBOT_DOG;
        return true;
    }
    if (strcasecmp(name, "r839") == 0 || strcasecmp(name, "839") == 0) {
        *out = IR_ROBOT_R839;
        return true;
    }
    return false;
}

static void save_to_nvs(ir_robot_type_t type)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed — robot type not persisted");
        return;
    }
    esp_err_t err = nvs_set_i32(handle, NVS_KEY, (int32_t)type);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS save failed: %s", esp_err_to_name(err));
    }
}

void ir_robot_type_load(void)
{
    if (s_loaded) {
        return;
    }

    /* Product default from Kconfig; overwrite stale NVS (e.g. old "dog") when default is r839. */
    ir_robot_type_t type = IR_ROBOT_DEFAULT;
#if !defined(CONFIG_IR_ROBOT_DEFAULT_R839)
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        int32_t stored = (int32_t)IR_ROBOT_DEFAULT;
        if (nvs_get_i32(handle, NVS_KEY, &stored) == ESP_OK && is_supported(stored)) {
            type = (ir_robot_type_t)stored;
        }
        nvs_close(handle);
    }
#else
    save_to_nvs(type);
#endif

    s_active = type;
    s_loaded = true;
    ESP_LOGI(TAG, "Active robot: %s", ir_robot_type_name(s_active));
}

ir_robot_type_t ir_robot_type_get(void)
{
    if (!s_loaded) {
        ir_robot_type_load();
    }
    return s_active;
}

bool ir_robot_type_set(ir_robot_type_t type)
{
    if (!is_supported((int32_t)type)) {
        return false;
    }
    s_active = type;
    s_loaded = true;
    save_to_nvs(type);
    ESP_LOGI(TAG, "Robot type set to: %s", ir_robot_type_name(type));
    return true;
}
