#include "chat_screen.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <wifi_manager.h>

#include <cmath>
#include <cstring>
#include <ctime>

#include "application.h"

#define TAG "KenChat"

namespace {

constexpr uint32_t kBg = 0x0B1220;
constexpr uint32_t kLine = 0x1E293B;
constexpr uint32_t kText = 0xF1F5F9;
constexpr uint32_t kMuted = 0x94A3B8;
constexpr uint32_t kUserBg = 0x2563EB;
constexpr uint32_t kBotBg = 0x1E293B;

constexpr int kStatusH = 30;
constexpr int kFaceCx = 64;    // centre of the face, screen coordinates
constexpr int kFaceCy = 138;
constexpr int kFaceD = 70;     // face circle diameter
constexpr int kRingIn = 42;    // ring of bars: inner radius
constexpr int kListX = 130;    // chat column starts here
constexpr int kBubbleMax = 158;  // text width inside a bubble

// Face shapes per emotion
enum MouthKind { kMouthLine, kMouthSmile, kMouthFrown, kMouthO };
struct FaceStyle {
    const char* names;   // space separated emotion names
    int eye_w, eye_h;
    int wink;            // 0 none, 1 right eye closed
    MouthKind mouth;
    int mouth_w;
    uint32_t color;
};

const FaceStyle kFaces[] = {
    {"happy laughing funny relaxed confident delicious", 12, 6, 0, kMouthSmile, 26, 0x67E8F9},
    {"loving kissy", 12, 6, 0, kMouthSmile, 20, 0xF9A8D4},
    {"winking silly", 12, 14, 1, kMouthSmile, 22, 0x67E8F9},
    {"cool", 16, 6, 0, kMouthLine, 18, 0x67E8F9},
    {"sad crying", 10, 10, 0, kMouthFrown, 22, 0x93C5FD},
    {"angry", 13, 8, 0, kMouthFrown, 24, 0xFCA5A5},
    {"surprised shocked", 13, 16, 0, kMouthO, 12, 0x67E8F9},
    {"embarrassed", 11, 9, 0, kMouthLine, 12, 0xF9A8D4},
    {"thinking confused", 11, 13, 0, kMouthLine, 12, 0xC4B5FD},
    {"sleepy", 13, 3, 0, kMouthLine, 10, 0x67E8F9},
};
const FaceStyle kNeutral = {"neutral", 12, 15, 0, kMouthLine, 18, 0x67E8F9};

const FaceStyle& FindFace(const std::string& emotion) {
    for (const auto& f : kFaces) {
        const char* p = strstr(f.names, emotion.c_str());
        if (p == nullptr) continue;
        size_t n = emotion.size();
        bool start_ok = p == f.names || p[-1] == ' ';
        bool end_ok = p[n] == '\0' || p[n] == ' ';
        if (start_ok && end_ok) return f;
    }
    return kNeutral;
}

lv_color_t Mix(uint32_t a, uint32_t b, float t) {
    return lv_color_mix(lv_color_hex(b), lv_color_hex(a), (uint8_t)(t * 255));
}

lv_obj_t* Box(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

}  // namespace

void KenChatDisplay::SetupUI() {
    if (setup_ui_called_) return;
    LcdDisplay::SetupUI();  // stock widgets stay underneath

    DisplayLockGuard lock(this);
    text_font_ = lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN);
    static lv_font_t f14 = lv_font_montserrat_14;
    f14.fallback = text_font_;
    small_font_ = &f14;

    const int W = width_, H = height_;
    chat_root_ = Box(lv_screen_active());
    lv_obj_set_size(chat_root_, W, H);
    lv_obj_set_style_bg_color(chat_root_, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(chat_root_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(chat_root_, text_font_, 0);
    lv_obj_set_style_text_color(chat_root_, lv_color_hex(kText), 0);

    // Status line: dot + status text, then time and Wi-Fi on the right
    dot_ = Box(chat_root_);
    lv_obj_set_size(dot_, 9, 9);
    lv_obj_set_pos(dot_, 10, 11);
    lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot_, lv_color_hex(kMuted), 0);

    status_lbl_ = lv_label_create(chat_root_);
    lv_obj_set_pos(status_lbl_, 25, 3);
    lv_obj_set_width(status_lbl_, W - 25 - 80);
    lv_label_set_long_mode(status_lbl_, LV_LABEL_LONG_DOT);
    lv_label_set_text(status_lbl_, "");

    wifi_lbl_ = lv_label_create(chat_root_);
    lv_obj_set_style_text_font(wifi_lbl_, small_font_, 0);
    lv_label_set_text(wifi_lbl_, LV_SYMBOL_WIFI);
    lv_obj_align(wifi_lbl_, LV_ALIGN_TOP_RIGHT, -8, 8);

    time_lbl_ = lv_label_create(chat_root_);
    lv_obj_set_style_text_font(time_lbl_, small_font_, 0);
    lv_label_set_text(time_lbl_, "--:--");
    lv_obj_align(time_lbl_, LV_ALIGN_TOP_RIGHT, -30, 8);

    lv_obj_t* line = Box(chat_root_);
    lv_obj_set_size(line, W, 1);
    lv_obj_set_pos(line, 0, kStatusH - 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(kLine), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    // Ring of bars around the face
    for (int i = 0; i < kTicks; i++) {
        float a = (float)i / kTicks;  // 0 = top, clockwise
        float from_top = a <= 0.5f ? a * 2 : (1 - a) * 2;  // 0 top .. 1 bottom
        lv_obj_t* l = lv_line_create(chat_root_);
        lv_obj_set_style_line_width(l, 3, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_set_style_line_color(l, Mix(0x818CF8, 0x22D3EE, from_top), 0);
        tick_[i] = l;
        tick_len_[i] = 5;
    }

    // Face: dark circle with a coloured rim, two eyes, a mouth
    face_ = Box(chat_root_);
    lv_obj_set_size(face_, kFaceD, kFaceD);
    lv_obj_set_pos(face_, kFaceCx - kFaceD / 2, kFaceCy - kFaceD / 2);
    lv_obj_set_style_radius(face_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(face_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(face_, lv_color_hex(0x0F1B2D), 0);
    lv_obj_set_style_border_width(face_, 3, 0);
    lv_obj_add_flag(face_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(face_, 20);
    lv_obj_add_event_cb(face_, [](lv_event_t*) {
        Application::GetInstance().Schedule([]() { Application::GetInstance().ToggleChatState(); });
    }, LV_EVENT_CLICKED, nullptr);

    for (auto& e : eye_) {
        e = Box(face_);
        lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(e, 5, 0);
    }
    mouth_rect_ = Box(face_);
    lv_obj_set_style_bg_opa(mouth_rect_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(mouth_rect_, LV_RADIUS_CIRCLE, 0);

    mouth_arc_ = lv_arc_create(face_);
    lv_obj_remove_style_all(mouth_arc_);
    lv_obj_clear_flag(mouth_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(mouth_arc_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(mouth_arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(mouth_arc_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(mouth_arc_, LV_OPA_TRANSP, LV_PART_MAIN);

    // Chat column
    list_ = lv_obj_create(chat_root_);
    lv_obj_remove_style_all(list_);
    lv_obj_set_pos(list_, kListX, kStatusH + 1);
    lv_obj_set_size(list_, W - kListX, H - kStatusH - 1);
    lv_obj_set_style_pad_all(list_, 4, 0);
    lv_obj_set_style_pad_right(list_, 6, 0);
    lv_obj_set_style_pad_row(list_, 6, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    // One-line system text (connecting, errors, activation code) at the bottom of the column
    note_ = lv_label_create(chat_root_);
    lv_obj_set_width(note_, W - kListX - 8);
    lv_obj_align(note_, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_label_set_long_mode(note_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(note_, lv_color_hex(kMuted), 0);
    lv_obj_set_style_bg_color(note_, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(note_, LV_OPA_90, 0);
    lv_obj_set_style_radius(note_, 8, 0);
    lv_obj_set_style_pad_all(note_, 4, 0);
    lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);

    ApplyFace();
    Animate();
    timer_ = lv_timer_create(TimerCb, 80, this);
}

void KenChatDisplay::TimerCb(lv_timer_t* t) {
    static_cast<KenChatDisplay*>(lv_timer_get_user_data(t))->Animate();
}

void KenChatDisplay::ApplyFace() {
    if (face_ == nullptr) return;
    const FaceStyle& f = FindFace(emotion_);
    lv_color_t c = lv_color_hex(f.color);
    lv_obj_set_style_border_color(face_, c, 0);

    bool blink = blink_left_ > 0;
    for (int i = 0; i < 2; i++) {
        int w = f.eye_w, h = f.eye_h;
        if (blink || (f.wink && i == 1)) h = 3;
        lv_obj_set_size(eye_[i], w, h);
        lv_obj_set_style_radius(eye_[i], h < 8 ? h / 2 : 5, 0);
        lv_obj_set_style_bg_color(eye_[i], c, 0);
        int cx = kFaceD / 2 - 3 + (i == 0 ? -13 : 13);  // -3: inside the 3 px rim
        int cy = kFaceD / 2 - 3 - 7;
        lv_obj_set_pos(eye_[i], cx - w / 2, cy - h / 2);
    }

    int mx = kFaceD / 2 - 3, my = kFaceD / 2 - 3 + 13;
    if (f.mouth == kMouthSmile || f.mouth == kMouthFrown) {
        lv_obj_add_flag(mouth_rect_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
        int d = f.mouth_w;
        lv_obj_set_size(mouth_arc_, d, d);
        lv_obj_set_style_arc_color(mouth_arc_, c, LV_PART_INDICATOR);
        lv_arc_set_bg_angles(mouth_arc_, 0, 360);
        if (f.mouth == kMouthSmile) {
            lv_arc_set_angles(mouth_arc_, 25, 155);  // lower part of a circle
            lv_obj_set_pos(mouth_arc_, mx - d / 2, my - d + 4);
        } else {
            lv_arc_set_angles(mouth_arc_, 205, 335);  // upper part
            lv_obj_set_pos(mouth_arc_, mx - d / 2, my - 2);
        }
    } else {
        lv_obj_add_flag(mouth_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mouth_rect_, LV_OBJ_FLAG_HIDDEN);
        int w = f.mouth_w, h = f.mouth == kMouthO ? f.mouth_w : 6;
        lv_obj_set_size(mouth_rect_, w, h);
        lv_obj_set_style_bg_color(mouth_rect_, c, 0);
        lv_obj_set_pos(mouth_rect_, mx - w / 2, my - h / 2);
    }
}

void KenChatDisplay::Animate() {
    // Nothing to do while the launcher (full screen, on the top layer) covers this screen
    lv_obj_t* top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        lv_obj_t* c = lv_obj_get_child(top, i);
        if (!lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) && lv_obj_get_width(c) >= width_ &&
            lv_obj_get_height(c) >= height_) {
            return;
        }
    }
    frame_++;
    auto state = Application::GetInstance().GetDeviceState();
    bool speaking = state == kDeviceStateSpeaking;
    bool listening = state == kDeviceStateListening;

    // Status dot: green while listening, cyan while speaking, grey otherwise
    uint32_t dot = listening ? 0x22C55E : speaking ? 0x22D3EE : kMuted;
    lv_obj_set_style_bg_color(dot_, lv_color_hex(dot), 0);
    if (notify_until_us_ != 0 && esp_timer_get_time() > notify_until_us_) {
        notify_until_us_ = 0;
        lv_label_set_text(status_lbl_, status_.c_str());
    }

    // Ring of bars: lively while speaking, a slow wave while listening, still otherwise.
    // This is an animation, not a measurement of the sound level.
    for (int i = 0; i < kTicks; i++) {
        float target = 4;
        if (speaking) {
            target = 5 + (esp_random() % 12);
        } else if (listening) {
            target = 6 + 4 * sinf(frame_ * 0.25f + i * (6.2832f / kTicks) * 2);
        }
        tick_len_[i] += (target - tick_len_[i]) * 0.45f;
        float a = (float)i / kTicks * 6.2832f;
        float sx = sinf(a), cy = -cosf(a);
        float r1 = kRingIn, r2 = kRingIn + tick_len_[i];
        tick_pts_[i][0].x = kFaceCx + sx * r1;
        tick_pts_[i][0].y = kFaceCy + cy * r1;
        tick_pts_[i][1].x = kFaceCx + sx * r2;
        tick_pts_[i][1].y = kFaceCy + cy * r2;
        lv_line_set_points(tick_[i], tick_pts_[i], 2);
        lv_obj_set_style_line_opa(tick_[i], (speaking || listening) ? LV_OPA_COVER : LV_OPA_50, 0);
    }

    // Blink every ~4 s
    if (blink_left_ > 0) {
        if (--blink_left_ == 0) ApplyFace();
    } else if (frame_ % 50 == 0) {
        blink_left_ = 2;
        ApplyFace();
    }

    // Clock and Wi-Fi once a second
    if (frame_ % 12 == 0) {
        time_t now = time(nullptr);
        struct tm tmv;
        localtime_r(&now, &tmv);
        if (tmv.tm_year + 1900 >= 2024) {
            lv_label_set_text_fmt(time_lbl_, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        }
        bool on = WifiManager::GetInstance().IsConnected();
        lv_obj_set_style_text_color(wifi_lbl_, lv_color_hex(on ? kText : 0x475569), 0);
    }
}

bool KenChatDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) return true;
    DisplayLockGuard lock(this);
    emotion_ = emotion;
    ApplyFace();
    return true;
}

void KenChatDisplay::SetStatus(const char* status) {
    LcdDisplay::SetStatus(status);
    DisplayLockGuard lock(this);
    status_ = status ? status : "";
    if (status_lbl_ && notify_until_us_ == 0) lv_label_set_text(status_lbl_, status_.c_str());
}

void KenChatDisplay::ShowNotification(const char* notification, int duration_ms) {
    LcdDisplay::ShowNotification(notification, duration_ms);
    DisplayLockGuard lock(this);
    if (status_lbl_ == nullptr || notification == nullptr) return;
    lv_label_set_text(status_lbl_, notification);
    notify_until_us_ = esp_timer_get_time() + (int64_t)duration_ms * 1000;
}

void KenChatDisplay::ClearChatMessages() {
    // Keep the conversation on screen after it ends; it is cleared when a new one
    // starts a couple of minutes later (see SetChatMessage)
    DisplayLockGuard lock(this);
    last_bubble_lbl_ = nullptr;
    SetNote("");
}

void KenChatDisplay::SetNote(const char* text) {
    if (note_ == nullptr) return;
    if (text == nullptr || text[0] == '\0') {
        lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(note_, text);
    lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
}

void KenChatDisplay::ScrollToEnd() {
    lv_obj_update_layout(list_);
    int32_t bottom = lv_obj_get_scroll_bottom(list_);
    if (bottom > 0) lv_obj_scroll_by(list_, 0, -bottom, LV_ANIM_ON);
}

void KenChatDisplay::AddBubble(bool user, const char* text) {
    // Drop the oldest rows
    while ((int)lv_obj_get_child_count(list_) >= kMaxRows) {
        lv_obj_t* first = lv_obj_get_child(list_, 0);
        lv_obj_delete(first);
    }
    lv_obj_t* row = Box(list_);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t* b = Box(row);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(user ? kUserBg : kBotBg), 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_pad_ver(b, 5, 0);

    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(l, kBubbleMax, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(l, lv_color_hex(user ? 0xFFFFFF : kText), 0);
    lv_label_set_text(l, text);

    last_bubble_lbl_ = user ? nullptr : l;
}

void KenChatDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (list_ == nullptr || role == nullptr) return;
    if (content == nullptr) content = "";

    if (strcmp(role, "system") == 0) {
        SetNote(content);
        return;
    }
    if (content[0] == '\0') return;

    bool user = strcmp(role, "user") == 0;
    // The firmware's own request to read a bank transaction aloud is not shown
    if (user && strncmp(content, "Đọc to đúng một câu", strlen("Đọc to đúng một câu")) == 0) return;

    int64_t now = esp_timer_get_time();
    if (last_msg_us_ != 0 && now - last_msg_us_ > 120LL * 1000000) {
        lv_obj_clean(list_);
        last_bubble_lbl_ = nullptr;
    }
    last_msg_us_ = now;
    SetNote("");

    // Sentences of one answer go into the same bubble
    if (!user && last_bubble_lbl_ != nullptr) {
        std::string text = lv_label_get_text(last_bubble_lbl_);
        if (text.size() + strlen(content) < 420) {
            text += " ";
            text += content;
            lv_label_set_text(last_bubble_lbl_, text.c_str());
            ScrollToEnd();
            return;
        }
    }
    AddBubble(user, content);
    ScrollToEnd();
}
