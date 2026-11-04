#include "nav_ui.h"

#ifdef CONFIG_PHONE_NAVIGATION_ENABLE

#include <cstring>
#include <string>

#include <esp_log.h>

#include "display.h"

#define TAG "NavUI"

LV_FONT_DECLARE(lv_font_montserrat_28);

static bool IsAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

void NavUI::Create(Display* display) {
    int w = display->width();
    int h = display->height();
    const lv_font_t* text_font = lv_obj_get_style_text_font(lv_screen_active(), 0);

    container_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, w, h);
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(container_, 10, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    // Turn arrow icon (48x48 shown at 2x)
    lv_color32_t* palette = (lv_color32_t*)icon_buf_;
    palette[0] = lv_color32_make(0, 0, 0, 0);          // background: transparent
    palette[1] = lv_color32_make(0x4A, 0xDE, 0x80, 0xFF);  // arrow: green
    icon_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon_dsc_.header.cf = LV_COLOR_FORMAT_I1;
    icon_dsc_.header.w = PHONE_NAV_ICON_SIZE;
    icon_dsc_.header.h = PHONE_NAV_ICON_SIZE;
    icon_dsc_.header.stride = PHONE_NAV_ICON_SIZE / 8;
    icon_dsc_.data_size = sizeof(icon_buf_);
    icon_dsc_.data = icon_buf_;

    icon_ = lv_image_create(container_);
    lv_image_set_src(icon_, &icon_dsc_);
    lv_image_set_scale(icon_, 512);
    lv_obj_set_size(icon_, PHONE_NAV_ICON_SIZE * 2, PHONE_NAV_ICON_SIZE * 2);
    lv_image_set_inner_align(icon_, LV_IMAGE_ALIGN_CENTER);
    lv_obj_align(icon_, LV_ALIGN_TOP_LEFT, 0, 0);

    // Distance to next turn (or title)
    title_ = lv_label_create(container_);
    lv_obj_set_style_text_color(title_, lv_color_white(), 0);
    lv_obj_set_width(title_, w - 20 - PHONE_NAV_ICON_SIZE * 2 - 10);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_WRAP);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, PHONE_NAV_ICON_SIZE * 2 + 10, 10);
    lv_label_set_text(title_, "");

    // Optional speed (OsmAnd only)
    speed_ = lv_label_create(container_);
    lv_obj_set_style_text_color(speed_, lv_color_hex(0xFACC15), 0);
    lv_obj_set_style_text_font(speed_, text_font, 0);
    lv_obj_align(speed_, LV_ALIGN_TOP_LEFT, PHONE_NAV_ICON_SIZE * 2 + 10, 60);
    lv_label_set_text(speed_, "");

    // Instruction / street name
    directions_ = lv_label_create(container_);
    lv_obj_set_style_text_color(directions_, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_text_font(directions_, text_font, 0);
    lv_obj_set_width(directions_, w - 20);
    lv_label_set_long_mode(directions_, LV_LABEL_LONG_WRAP);
    lv_obj_align(directions_, LV_ALIGN_TOP_LEFT, 0, PHONE_NAV_ICON_SIZE * 2 + 8);
    lv_label_set_text(directions_, "");

    // Remaining time / distance / ETA
    footer_ = lv_label_create(container_);
    lv_obj_set_style_text_color(footer_, lv_color_hex(0x93C5FD), 0);
    lv_obj_set_style_text_font(footer_, text_font, 0);
    lv_obj_set_width(footer_, w - 20);
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_DOT);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(footer_, "");
}

void NavUI::Update(Display* display, const PhoneNavState& state, bool icon_changed) {
    if (container_ == nullptr) {
        Create(display);
    }

    bool show = state.connected && state.active;
    if (!show) {
        if (visible_) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        return;
    }

    if (icon_changed || !state.has_icon) {
        if (state.has_icon) {
            memcpy(icon_buf_ + 8, state.icon, PHONE_NAV_ICON_BYTES);
            lv_image_cache_drop(&icon_dsc_);
            lv_image_set_src(icon_, &icon_dsc_);
            lv_obj_remove_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const lv_font_t* text_font = lv_obj_get_style_text_font(lv_screen_active(), 0);
    lv_obj_set_style_text_font(title_, IsAscii(state.title) ? &lv_font_montserrat_28 : text_font, 0);
    lv_label_set_text(title_, state.title.c_str());
    lv_label_set_text(speed_, state.speed.c_str());
    lv_label_set_text(directions_, state.directions.c_str());

    std::string footer;
    auto add = [&footer](const std::string& part) {
        if (part.empty()) return;
        if (!footer.empty()) footer += "  |  ";
        footer += part;
    };
    add(state.duration);
    add(state.distance);
    add(state.eta);
    lv_label_set_text(footer_, footer.c_str());

    if (!visible_) {
        lv_obj_remove_flag(container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(container_);
        visible_ = true;
    }
}

#endif  // CONFIG_PHONE_NAVIGATION_ENABLE
