#ifndef KENDEV_CHAT_SCREEN_H
#define KENDEV_CHAT_SCREEN_H

// Conversation screen for the 2.8" KenDev board: a robot face with expressions on
// the left (ring of bars that moves while listening / speaking), chat bubbles on
// the right (user blue, assistant grey), status line on top.
// It replaces the stock emoji + one-line subtitle layout; the stock widgets are still
// created underneath so the rest of the firmware keeps working unchanged.

#include "display/lcd_display.h"

#include <cstdint>
#include <string>

class KenChatDisplay : public SpiLcdDisplay {
public:
    KenChatDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                        swap_xy) {}

    void SetupUI() override;
    bool SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetStatus(const char* status) override;
    using LvglDisplay::ShowNotification;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;

private:
    static constexpr int kTicks = 28;
    static constexpr int kMaxRows = 10;

    static void TimerCb(lv_timer_t* t);
    void Animate();
    void ApplyFace();
    void AddBubble(bool user, const char* text);
    void ScrollToEnd();
    void SetNote(const char* text);

    lv_obj_t* chat_root_ = nullptr;
    lv_obj_t* dot_ = nullptr;
    lv_obj_t* status_lbl_ = nullptr;
    lv_obj_t* time_lbl_ = nullptr;
    lv_obj_t* wifi_lbl_ = nullptr;
    lv_obj_t* face_ = nullptr;
    lv_obj_t* eye_[2] = {nullptr, nullptr};
    lv_obj_t* mouth_rect_ = nullptr;
    lv_obj_t* mouth_arc_ = nullptr;
    lv_obj_t* tick_[kTicks] = {};
    lv_point_precise_t tick_pts_[kTicks][2] = {};
    float tick_len_[kTicks] = {};
    lv_obj_t* list_ = nullptr;
    lv_obj_t* note_ = nullptr;
    lv_obj_t* last_bubble_lbl_ = nullptr;  // assistant bubble that is still growing
    lv_timer_t* timer_ = nullptr;

    std::string emotion_ = "neutral";
    std::string status_;
    int64_t last_msg_us_ = 0;
    int64_t notify_until_us_ = 0;
    uint32_t frame_ = 0;
    int blink_left_ = 0;
    const lv_font_t* text_font_ = nullptr;
    const lv_font_t* small_font_ = nullptr;
};

#endif  // KENDEV_CHAT_SCREEN_H
