// Lenient FT6x36/FT6336 touch driver for esp_lcd_touch.
// Same register handling as cfscn/esp_lcd_touch_ft6x36 (Apache-2.0), but it does
// NOT reject controllers whose panel/vendor ID (0xA8) is not 0x11: many FT6336U
// modules on ES3C28P boards report another vendor ID, which made the original
// driver fail and left the touch screen dead.
#pragma once
#include <esp_lcd_panel_io.h>
#include <esp_lcd_touch.h>

#ifdef __cplusplus
extern "C" {
#endif

// vend_id / chip_id (optional) receive registers 0xA8 / 0xA3.
esp_err_t ft6336_touch_new(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t* config,
                           esp_lcd_touch_handle_t* out_touch, uint8_t* vend_id, uint8_t* chip_id);

#ifdef __cplusplus
}
#endif
