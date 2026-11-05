#include "weather_icon.h"

#include <initializer_list>

enum { kNone = -1, kSun, kMoon, kPartly, kCloud, kRain, kStorm, kFog };

static int KindFor(int code, bool is_day) {
    if (code < 0) return kNone;
    if (code <= 1) return is_day ? kSun : kMoon;
    if (code == 2) return kPartly;
    if (code == 3) return kCloud;
    if (code == 45 || code == 48) return kFog;
    if (code >= 95) return kStorm;
    return kRain;
}

WeatherIcon::WeatherIcon(lv_obj_t* parent, int size) : size_(size) {
    box_ = lv_obj_create(parent);
    lv_obj_remove_style_all(box_);
    lv_obj_set_size(box_, size, size);
    lv_obj_clear_flag(box_, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    for (auto& d : dots_) {
        d = lv_obj_create(box_);
        lv_obj_remove_style_all(d);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    }
    block_ = lv_obj_create(box_);
    lv_obj_remove_style_all(block_);
    lv_obj_set_style_bg_opa(block_, LV_OPA_COVER, 0);
    lv_obj_add_flag(block_, LV_OBJ_FLAG_HIDDEN);
    for (auto& l : lines_) {
        l = lv_line_create(box_);
        lv_obj_set_style_line_width(l, size >= 40 ? 3 : 2, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
}

void WeatherIcon::Set(int code, bool is_day) {
    int kind = KindFor(code, is_day);
    if (kind == kind_) return;
    kind_ = kind;
    for (auto* d : dots_) lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(block_, LV_OBJ_FLAG_HIDDEN);
    for (auto* l : lines_) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);

    const lv_color_t sun = lv_color_hex(0xFBBF24), cloud = lv_color_hex(0xE5E7EB),
                     cloud_dark = lv_color_hex(0x9CA3AF), rain = lv_color_hex(0x60A5FA),
                     moon = lv_color_hex(0xFDE68A), bg = lv_color_hex(0x0B1220);
    auto circle = [this](lv_obj_t* o, float x, float y, float d, lv_color_t c) {
        lv_obj_set_pos(o, S(x), S(y));
        lv_obj_set_size(o, S(d), S(d));
        lv_obj_set_style_bg_color(o, c, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(o);
    };
    int nl = 0;
    auto line = [this, &nl](std::initializer_list<lv_point_precise_t> p, lv_color_t c) {
        if (nl >= 4) return;
        int n = 0;
        for (auto& q : p) {
            if (n < 3) pts_[nl][n++] = {(lv_value_precise_t)S(q.x), (lv_value_precise_t)S(q.y)};
        }
        lv_line_set_points(lines_[nl], pts_[nl], n);
        lv_obj_set_style_line_color(lines_[nl], c, 0);
        lv_obj_remove_flag(lines_[nl], LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(lines_[nl]);
        nl++;
    };
    auto cloud_at = [&](float dy, lv_color_t c) {
        circle(dots_[0], 2, 9 + dy, 10, c);
        circle(dots_[1], 8, 4 + dy, 13, c);
        lv_obj_set_pos(block_, S(4), S(12 + dy));
        lv_obj_set_size(block_, S(18), S(7));
        lv_obj_set_style_bg_color(block_, c, 0);
        lv_obj_set_style_radius(block_, S(3), 0);
        lv_obj_remove_flag(block_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(dots_[0]);
        lv_obj_move_foreground(dots_[1]);
    };

    switch (kind) {
        case kSun:
            circle(dots_[0], 6, 6, 12, sun);
            line({{12, 0}, {12, 3}}, sun);
            line({{12, 21}, {12, 24}}, sun);
            line({{0, 12}, {3, 12}}, sun);
            line({{21, 12}, {24, 12}}, sun);
            break;
        case kMoon:
            circle(dots_[0], 3, 3, 17, moon);
            circle(dots_[1], 9, 0, 15, bg);
            break;
        case kPartly:
            circle(dots_[2], 11, 0, 11, sun);
            cloud_at(4, cloud);
            break;
        case kCloud:
            cloud_at(2, cloud);
            break;
        case kRain:
            cloud_at(-2, cloud_dark);
            line({{7, 19}, {5, 23}}, rain);
            line({{13, 19}, {11, 23}}, rain);
            line({{19, 19}, {17, 23}}, rain);
            break;
        case kStorm:
            cloud_at(-2, cloud_dark);
            line({{14, 17}, {10, 21}, {15, 21}}, sun);
            line({{15, 21}, {11, 24}}, sun);
            break;
        case kFog:
            line({{2, 7}, {22, 7}}, cloud_dark);
            line({{2, 12}, {22, 12}}, cloud_dark);
            line({{2, 17}, {22, 17}}, cloud_dark);
            break;
        default:
            break;
    }
}
