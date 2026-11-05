#include "nav_ui.h"

#ifdef CONFIG_PHONE_NAVIGATION_ENABLE

#include <algorithm>
#include <cmath>
#include <string>

#include <esp_log.h>

#include "display.h"

#define TAG "NavUI"

LV_FONT_DECLARE(lv_font_montserrat_28);

static const uint32_t kArrowColor = 0x4ADE80;   // green
static const uint32_t kGrayColor = 0x6B7280;
static const uint32_t kRingColor = 0x9CA3AF;
static const int kLineWidth = 14;

static bool IsAscii(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

static lv_obj_t* MakeLine(lv_obj_t* parent, uint32_t color, int width) {
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

void NavUI::Create(Display* display) {
    int w = display->width();
    int h = display->height();
    const lv_font_t* text_font = lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN);

    container_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, w, h);
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x0B1220), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(container_, 8, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    // Maneuver drawing area
    box_ = lv_obj_create(container_);
    lv_obj_remove_style_all(box_);
    lv_obj_set_size(box_, kBox, kBox);
    lv_obj_set_style_bg_color(box_, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box_, 16, 0);
    lv_obj_clear_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(box_, LV_ALIGN_TOP_LEFT, 0, 0);

    for (int i = 0; i < kAux; i++) aux_[i] = MakeLine(box_, kGrayColor, 6);

    ring_ = lv_arc_create(box_);
    lv_obj_remove_style_all(ring_);
    lv_obj_set_size(ring_, 62, 62);
    lv_arc_set_bg_angles(ring_, 0, 360);
    lv_arc_set_angles(ring_, 0, 0);
    lv_obj_set_style_arc_width(ring_, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring_, lv_color_hex(kRingColor), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring_, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(ring_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ring_, 65 - 31, 62 - 31);
    lv_obj_add_flag(ring_, LV_OBJ_FLAG_HIDDEN);

    ring_label_ = lv_label_create(box_);
    lv_obj_set_style_text_font(ring_label_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ring_label_, lv_color_white(), 0);
    lv_label_set_text(ring_label_, "");
    lv_obj_align(ring_label_, LV_ALIGN_TOP_MID, 0, 62 - 16);
    lv_obj_add_flag(ring_label_, LV_OBJ_FLAG_HIDDEN);

    path_ = MakeLine(box_, kArrowColor, kLineWidth);
    head_ = MakeLine(box_, kArrowColor, kLineWidth);

    const int right_x = kBox + 10;
    const int right_w = w - 16 - right_x;

    // Distance to the next maneuver
    title_ = lv_label_create(container_);
    lv_obj_set_style_text_color(title_, lv_color_white(), 0);
    lv_obj_set_width(title_, right_w);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(title_, right_x, 2);
    lv_label_set_text(title_, "");

    // Instruction / street name
    directions_ = lv_label_create(container_);
    lv_obj_set_style_text_color(directions_, lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_text_font(directions_, text_font, 0);
    lv_obj_set_width(directions_, right_w);
    lv_obj_set_height(directions_, kBox - 40);
    lv_label_set_long_mode(directions_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(directions_, right_x, 40);
    lv_label_set_text(directions_, "");

    // "Getting close" bar: fills up as the maneuver approaches
    bar_ = lv_bar_create(container_);
    lv_obj_set_size(bar_, w - 16, 16);
    lv_obj_set_pos(bar_, 0, kBox + 12);
    lv_bar_set_range(bar_, 0, 100);
    lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(kArrowColor), LV_PART_INDICATOR);
    lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);

    // Remaining time / distance / ETA
    footer_ = lv_label_create(container_);
    lv_obj_set_style_text_color(footer_, lv_color_hex(0x93C5FD), 0);
    lv_obj_set_style_text_font(footer_, text_font, 0);
    lv_obj_set_width(footer_, w - 16);
    lv_label_set_long_mode(footer_, LV_LABEL_LONG_DOT);
    lv_obj_align(footer_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(footer_, "");
}

void NavUI::HideAll() {
    lv_obj_add_flag(path_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(head_, LV_OBJ_FLAG_HIDDEN);
    for (auto* a : aux_) lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ring_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ring_label_, LV_OBJ_FLAG_HIDDEN);
}

void NavUI::SetPath(std::initializer_list<lv_point_precise_t> pts, bool with_head) {
    int n = 0;
    for (auto& p : pts) {
        if (n < kMaxPath) path_pts_[n++] = p;
    }
    lv_line_set_points(path_, path_pts_, n);
    lv_obj_remove_flag(path_, LV_OBJ_FLAG_HIDDEN);
    if (!with_head || n < 2) return;

    // Chevron arrow head along the last segment
    float ax = path_pts_[n - 2].x, ay = path_pts_[n - 2].y;
    float bx = path_pts_[n - 1].x, by = path_pts_[n - 1].y;
    float len = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    if (len < 1) return;
    float ux = (bx - ax) / len, uy = (by - ay) / len;
    float back_x = bx - ux * 24, back_y = by - uy * 24;
    float nx = -uy, ny = ux;
    head_pts_[0] = {(lv_value_precise_t)(back_x + nx * 20), (lv_value_precise_t)(back_y + ny * 20)};
    head_pts_[1] = {(lv_value_precise_t)bx, (lv_value_precise_t)by};
    head_pts_[2] = {(lv_value_precise_t)(back_x - nx * 20), (lv_value_precise_t)(back_y - ny * 20)};
    lv_line_set_points(head_, head_pts_, 3);
    lv_obj_remove_flag(head_, LV_OBJ_FLAG_HIDDEN);
}

void NavUI::SetAux(int i, lv_point_precise_t a, lv_point_precise_t b, uint32_t color, int width) {
    aux_pts_[i][0] = a;
    aux_pts_[i][1] = b;
    lv_line_set_points(aux_[i], aux_pts_[i], 2);
    lv_obj_set_style_line_color(aux_[i], lv_color_hex(color), 0);
    lv_obj_set_style_line_width(aux_[i], width, 0);
    lv_obj_remove_flag(aux_[i], LV_OBJ_FLAG_HIDDEN);
}

void NavUI::DrawManeuver(const ManeuverInfo& m) {
    HideAll();
    switch (m.type) {
        case Maneuver::kStraight:
            SetPath({{65, 118}, {65, 18}}, true);
            break;
        case Maneuver::kLeft:
            SetPath({{84, 118}, {84, 62}, {20, 62}}, true);
            break;
        case Maneuver::kRight:
            SetPath({{46, 118}, {46, 62}, {110, 62}}, true);
            break;
        case Maneuver::kSlightLeft:
            SetPath({{80, 118}, {80, 80}, {34, 26}}, true);
            break;
        case Maneuver::kSlightRight:
            SetPath({{50, 118}, {50, 80}, {96, 26}}, true);
            break;
        case Maneuver::kSharpLeft:
            SetPath({{84, 118}, {84, 36}, {28, 96}}, true);
            break;
        case Maneuver::kSharpRight:
            SetPath({{46, 118}, {46, 36}, {102, 96}}, true);
            break;
        case Maneuver::kKeepLeft:
            SetAux(0, {84, 118}, {84, 16}, kGrayColor, 8);
            SetPath({{84, 118}, {84, 82}, {40, 30}}, true);
            break;
        case Maneuver::kKeepRight:
            SetAux(0, {46, 118}, {46, 16}, kGrayColor, 8);
            SetPath({{46, 118}, {46, 82}, {90, 30}}, true);
            break;
        case Maneuver::kUTurn:
            // Vietnam drives on the right: U-turns go round to the left
            SetPath({{90, 118}, {90, 60}, {87, 45}, {78, 35}, {65, 31}, {52, 35},
                     {43, 45}, {40, 60}, {40, 104}}, true);
            break;
        case Maneuver::kRoundabout: {
            lv_obj_remove_flag(ring_, LV_OBJ_FLAG_HIDDEN);
            SetAux(0, {65, 126}, {65, 96}, kArrowColor, 12);  // entry
            // Counter-clockwise roundabout entered from the bottom:
            // exit 1 = right, 2 = straight on, 3 = left, 4+ = back down-left
            int n = m.exit_number;
            float deg = n == 1 ? 90 : n == 3 ? -90 : n >= 4 ? -140 : 0;
            float rad = deg * 3.14159265f / 180.0f;
            float dx = std::sin(rad), dy = -std::cos(rad);
            float r0 = 31, r1 = 60;
            if (n == 1 || n == 3) r1 = 58;
            SetPath({{(lv_value_precise_t)(65 + dx * r0), (lv_value_precise_t)(62 + dy * r0)},
                     {(lv_value_precise_t)(65 + dx * r1), (lv_value_precise_t)(62 + dy * r1)}}, true);
            if (n > 0) {
                lv_label_set_text_fmt(ring_label_, "%d", n);
                lv_obj_remove_flag(ring_label_, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        }
        case Maneuver::kRampUp:
            // Ground, two pillars, and the arrow climbing onto the bridge
            SetAux(0, {6, 122}, {124, 122}, kGrayColor, 6);
            SetAux(1, {70, 122}, {70, 86}, kGrayColor, 6);
            SetAux(2, {96, 122}, {96, 60}, kGrayColor, 6);
            SetPath({{10, 104}, {44, 104}, {112, 38}}, true);
            break;
        case Maneuver::kRampDown:
            // Bridge deck on pillars, arrow going down to the ground
            SetAux(0, {6, 122}, {124, 122}, kGrayColor, 6);
            SetAux(1, {18, 122}, {18, 48}, kGrayColor, 6);
            SetAux(2, {44, 122}, {44, 48}, kGrayColor, 6);
            SetPath({{10, 38}, {50, 38}, {112, 102}}, true);
            break;
        case Maneuver::kArrive:
            lv_obj_set_style_arc_color(ring_, lv_color_hex(0xF87171), LV_PART_MAIN);
            lv_obj_remove_flag(ring_, LV_OBJ_FLAG_HIDDEN);
            SetAux(0, {65, 62}, {65, 62}, 0xF87171, 22);  // centre dot
            SetPath({{65, 126}, {65, 100}}, false);
            break;
    }
    if (m.type != Maneuver::kArrive) {
        lv_obj_set_style_arc_color(ring_, lv_color_hex(kRingColor), LV_PART_MAIN);
    }
}

void NavUI::UpdateProgress(const PhoneNavState& state) {
    int d = ParseDistanceMeters(state.title);
    if (!state.is_navigation || d < 0) {
        lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // New instruction: the bar starts filling within 500 m of the maneuver
    if (state.directions != last_directions_ || start_distance_ < 0 || d > start_distance_) {
        last_directions_ = state.directions;
        start_distance_ = std::clamp(d, 100, 500);
    }
    int pct = d >= start_distance_ ? 0 : (start_distance_ - d) * 100 / start_distance_;
    if (d <= 20) pct = 100;
    uint32_t color = d <= 30 ? 0xEF4444 : d <= 100 ? 0xFACC15 : kArrowColor;
    lv_obj_set_style_bg_color(bar_, lv_color_hex(color), LV_PART_INDICATOR);
    lv_bar_set_value(bar_, pct, LV_ANIM_ON);
    lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
}

void NavUI::Update(Display* display, const PhoneNavState& state, bool /*icon_changed*/) {
    if (container_ == nullptr) {
        Create(display);
    }

    bool show = state.connected && state.active;
    if (!show) {
        if (visible_) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        last_directions_.clear();
        start_distance_ = -1;
        return;
    }

    if (state.is_navigation) {
        ManeuverInfo m = ClassifyManeuver(state.directions);
        if ((int)m.type != last_type_ || m.exit_number != last_exit_) {
            DrawManeuver(m);
            last_type_ = (int)m.type;
            last_exit_ = m.exit_number;
        }
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
        last_type_ = -1;
    }

    const lv_font_t* text_font = lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_, IsAscii(state.title) ? &lv_font_montserrat_28 : text_font, 0);
    std::string title = state.title;
    if (!state.speed.empty()) title += "  " + state.speed;
    lv_label_set_text(title_, title.c_str());
    lv_label_set_text(directions_, state.directions.c_str());
    UpdateProgress(state);

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
