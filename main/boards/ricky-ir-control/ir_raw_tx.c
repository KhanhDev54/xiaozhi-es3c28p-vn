#include "ir_raw_tx.h"
#include "ir_common.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "IrRawTx";

#define IR_RESOLUTION_HZ 1000000

esp_err_t ir_raw_tx_init(rmt_channel_handle_t *out_channel, rmt_encoder_handle_t *out_copy_encoder, int gpio_num)
{
    ESP_RETURN_ON_FALSE(out_channel && out_copy_encoder, ESP_ERR_INVALID_ARG, TAG, "null output");

    rmt_tx_channel_config_t tx_channel_cfg = {
        .gpio_num = gpio_num,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = IR_RMT_MEM_BLOCK_SYMBOLS,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_channel_cfg, out_channel), TAG, "create tx channel failed");

    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33,
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(*out_channel, &carrier_cfg), TAG, "apply carrier failed");

    rmt_copy_encoder_config_t copy_encoder_cfg = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_cfg, out_copy_encoder), TAG, "create copy encoder failed");
    ESP_RETURN_ON_ERROR(rmt_enable(*out_channel), TAG, "enable tx channel failed");
    return ESP_OK;
}

esp_err_t ir_raw_tx_send(rmt_channel_handle_t channel, rmt_encoder_handle_t copy_encoder,
                         const rmt_symbol_word_t *symbols, size_t count, int repeat_count)
{
    ESP_RETURN_ON_FALSE(channel && copy_encoder && symbols && count > 0, ESP_ERR_INVALID_ARG, TAG, "invalid args");

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0, /* idle LOW — IR LED off after TX */
        },
    };

    for (int i = 0; i < repeat_count; i++) {
        ESP_RETURN_ON_ERROR(
            rmt_transmit(channel, copy_encoder, symbols, count * sizeof(rmt_symbol_word_t), &transmit_config),
            TAG, "transmit failed");
        ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(channel, 1000), TAG, "wait tx done failed");
        if (i + 1 < repeat_count) {
            vTaskDelay(pdMS_TO_TICKS(110));
        }
    }
    return ESP_OK;
}

esp_err_t ir_raw_tx_send_burst(rmt_channel_handle_t channel, rmt_encoder_handle_t copy_encoder,
                               const rmt_symbol_word_t *symbols, size_t count, int duration_ms)
{
    ESP_RETURN_ON_FALSE(channel && copy_encoder && symbols && count > 0, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    if (duration_ms <= 0) {
        duration_ms = 1000;
    }

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0, /* idle LOW — IR LED off after TX */
        },
    };

    const int64_t end_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;
    int sent = 0;
    while (esp_timer_get_time() < end_us) {
        ESP_RETURN_ON_ERROR(
            rmt_transmit(channel, copy_encoder, symbols, count * sizeof(rmt_symbol_word_t), &transmit_config),
            TAG, "burst transmit failed");
        ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(channel, 1000), TAG, "burst wait failed");
        sent++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "Burst sent %d frames over %d ms", sent, duration_ms);
    return ESP_OK;
}

void ir_raw_tx_deinit(rmt_channel_handle_t channel, rmt_encoder_handle_t copy_encoder)
{
    if (copy_encoder) {
        rmt_del_encoder(copy_encoder);
    }
    if (channel) {
        rmt_disable(channel);
        rmt_del_channel(channel);
    }
}
