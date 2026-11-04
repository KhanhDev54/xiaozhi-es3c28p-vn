#ifndef NAV_UI_H
#define NAV_UI_H

#include <sdkconfig.h>

#ifdef CONFIG_PHONE_NAVIGATION_ENABLE

#include <lvgl.h>

#include "phone_nav_ble.h"

class Display;

// Full-screen navigation overlay drawn on LVGL's top layer, so it covers the
// chat / weather UI while navigation is running and disappears afterwards.
class NavUI {
public:
    static NavUI& GetInstance() {
        static NavUI instance;
        return instance;
    }

    // Must be called with the display locked (or before LVGL is shared)
    void Update(Display* display, const PhoneNavState& state, bool icon_changed);
    bool IsVisible() const { return visible_; }

private:
    NavUI() = default;
    void Create(Display* display);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* icon_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* directions_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    lv_obj_t* speed_ = nullptr;
    bool visible_ = false;

    // 1bpp indexed image: 2-entry palette (8 bytes) + 48x48 bits
    uint8_t icon_buf_[8 + PHONE_NAV_ICON_BYTES] = {};
    lv_image_dsc_t icon_dsc_ = {};
};

#endif  // CONFIG_PHONE_NAVIGATION_ENABLE
#endif  // NAV_UI_H
