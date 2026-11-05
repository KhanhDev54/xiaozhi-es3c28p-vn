#ifndef NAV_UI_H
#define NAV_UI_H

#include <sdkconfig.h>

#ifdef CONFIG_PHONE_NAVIGATION_ENABLE

#include <lvgl.h>
#include <initializer_list>
#include <string>
#include <vector>

#include "nav_maneuver.h"
#include "phone_nav_ble.h"

class Display;

// Full-screen navigation overlay drawn on LVGL's top layer.
// The maneuver arrow is drawn with LVGL lines/arcs (no bitmap from the phone):
// the instruction text from Google Maps decides the shape (turn left/right,
// slight/sharp, U-turn, roundabout with exit number, up/down a bridge ramp,
// arrive). A bar fills up as the turn gets closer.
class NavUI {
public:
    static NavUI& GetInstance() {
        static NavUI instance;
        return instance;
    }

    // Must be called with the display locked
    void Update(Display* display, const PhoneNavState& state, bool icon_changed);
    bool IsVisible() const { return visible_; }

private:
    NavUI() = default;
    void Create(Display* display);
    void DrawManeuver(const ManeuverInfo& m);
    void SetPath(std::initializer_list<lv_point_precise_t> pts, bool with_head);
    void SetAux(int i, lv_point_precise_t a, lv_point_precise_t b, uint32_t color, int width);
    void HideAll();
    void UpdateProgress(const PhoneNavState& state);

    static constexpr int kBox = 130;
    static constexpr int kMaxPath = 12;
    static constexpr int kAux = 3;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* box_ = nullptr;
    lv_obj_t* path_ = nullptr;
    lv_obj_t* head_ = nullptr;
    lv_obj_t* aux_[kAux] = {};
    lv_obj_t* ring_ = nullptr;
    lv_obj_t* ring_label_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* directions_ = nullptr;
    lv_obj_t* bar_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    bool visible_ = false;

    lv_point_precise_t path_pts_[kMaxPath] = {};
    lv_point_precise_t head_pts_[3] = {};
    lv_point_precise_t aux_pts_[kAux][2] = {};

    std::string last_directions_;
    int start_distance_ = -1;
    int last_type_ = -1;
    int last_exit_ = -1;
};

#endif  // CONFIG_PHONE_NAVIGATION_ENABLE
#endif  // NAV_UI_H
