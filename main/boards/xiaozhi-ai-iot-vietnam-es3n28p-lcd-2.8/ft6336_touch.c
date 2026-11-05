/*
 * Based on cfscn/esp_lcd_touch_ft6x36 (SPDX-License-Identifier: Apache-2.0,
 * Copyright 2025 CFSoft Systems (Chengdu) CO LTD). Modified: no vendor/chip ID
 * rejection, no reset handling (the board resets the controller itself).
 */
#include "ft6336_touch.h"

#include <stdlib.h>
#include <string.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char* TAG = "FT6336";

static esp_err_t rd(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t* data, uint8_t len) {
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t wr(esp_lcd_touch_handle_t tp, uint8_t reg, uint8_t val) {
    return esp_lcd_panel_io_tx_param(tp->io, reg, &val, 1);
}

// Diagnostics: a summary line every ~5 s of polling, and every press is logged
static uint32_t s_reads, s_errors, s_presses;
static uint8_t s_last_td;

static esp_err_t ft_read_data(esp_lcd_touch_handle_t tp) {
    uint8_t data[7];
    s_reads++;
    if (s_reads % 500 == 0) {
        ESP_LOGD(TAG, "diag: reads=%lu errors=%lu presses=%lu last_td=0x%02X",
                 (unsigned long)s_reads, (unsigned long)s_errors, (unsigned long)s_presses, s_last_td);
    }
    esp_err_t err = rd(tp, 0x00, data, sizeof(data));
    if (err != ESP_OK) {
        if (s_errors++ % 50 == 0) ESP_LOGE(TAG, "I2C read error: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t points = data[0x02] & 0x0F;
    if (data[0x02] != s_last_td) {
        if (points > 0 && (s_last_td & 0x0F) == 0) {
            s_presses++;
            ESP_LOGD(TAG, "press raw: td=0x%02X x=%d y=%d", data[0x02],
                     ((data[0x03] & 0x0F) << 8) | data[0x04], ((data[0x05] & 0x0F) << 8) | data[0x06]);
        }
        s_last_td = data[0x02];
    }
    if (points == 0 || points > 2) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = 1;  // first point is enough for gestures
    tp->data.coords[0].x = ((data[0x03] & 0x0F) << 8) | data[0x04];
    tp->data.coords[0].y = ((data[0x05] & 0x0F) << 8) | data[0x06];
    tp->data.coords[0].strength = 0;
    portEXIT_CRITICAL(&tp->data.lock);
    return ESP_OK;
}

static bool ft_get_xy(esp_lcd_touch_handle_t tp, uint16_t* x, uint16_t* y, uint16_t* strength,
                      uint8_t* point_num, uint8_t max_point_num) {
    portENTER_CRITICAL(&tp->data.lock);
    *point_num = tp->data.points > max_point_num ? max_point_num : tp->data.points;
    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) strength[i] = tp->data.coords[i].strength;
    }
    tp->data.points = 0;
    portEXIT_CRITICAL(&tp->data.lock);
    return *point_num > 0;
}

static esp_err_t ft_del(esp_lcd_touch_handle_t tp) {
    free(tp);
    return ESP_OK;
}

esp_err_t ft6336_touch_new(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t* config,
                           esp_lcd_touch_handle_t* out_touch, uint8_t* vend_id, uint8_t* chip_id) {
    esp_lcd_touch_handle_t tp = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    if (tp == NULL) return ESP_ERR_NO_MEM;
    tp->io = io;
    tp->read_data = ft_read_data;
    tp->get_xy = ft_get_xy;
    tp->del = ft_del;
    tp->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&tp->config, config, sizeof(esp_lcd_touch_config_t));

    uint8_t vend = 0, chip = 0, fw = 0;
    esp_err_t ret = rd(tp, 0xA8, &vend, 1);
    if (ret == ESP_OK) ret = rd(tp, 0xA3, &chip, 1);
    if (ret == ESP_OK) ret = rd(tp, 0xA6, &fw, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Controller does not answer: %s", esp_err_to_name(ret));
        free(tp);
        return ret;
    }
    ESP_LOGI(TAG, "Vendor 0x%02X, chip 0x%02X, firmware %d", vend, chip, fw);
    if (vend_id) *vend_id = vend;
    if (chip_id) *chip_id = chip;

    wr(tp, 0x00, 0x00);  // DEV_MODE: working mode
    wr(tp, 0x80, 22);    // touch threshold (same as known-good ES3C28P code)
    wr(tp, 0x86, 0x00);  // CTRL: stay in active mode
    wr(tp, 0x88, 12);    // report rate
    wr(tp, 0xA4, 0x00);  // G_MODE: polling

    *out_touch = tp;
    return ESP_OK;
}
