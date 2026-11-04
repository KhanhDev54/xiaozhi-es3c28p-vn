#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ir_raw_tx_init(rmt_channel_handle_t *out_channel, rmt_encoder_handle_t *out_copy_encoder, int gpio_num);
esp_err_t ir_raw_tx_send(rmt_channel_handle_t channel, rmt_encoder_handle_t copy_encoder,
                         const rmt_symbol_word_t *symbols, size_t count, int repeat_count);
esp_err_t ir_raw_tx_send_burst(rmt_channel_handle_t channel, rmt_encoder_handle_t copy_encoder,
                               const rmt_symbol_word_t *symbols, size_t count, int duration_ms);
void ir_raw_tx_deinit(rmt_channel_handle_t channel, rmt_encoder_handle_t copy_encoder);

#ifdef __cplusplus
}
#endif
