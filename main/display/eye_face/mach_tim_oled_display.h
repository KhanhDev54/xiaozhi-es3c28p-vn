#pragma once

#include <string>

#include "display/oled_display.h"
#include "mach_tim_eye_assets.h"

/** Bo cuc man hinh: mat chiem toan bo, hoac kieu wechat (text + icon nho khi noi). */
enum class MachTimFaceLayout {
    kFullFace = 0,
    kWechat = 1,
};

const char* MachTimFaceLayoutName(MachTimFaceLayout layout);
bool MachTimFaceLayoutParse(const char* name, MachTimFaceLayout* out);

/**
 * OLED 0.96" MachTim Otto — mắt esp-hi (AAF → I1 bitmap animation).
 */
class MachTimOledDisplay : public OledDisplay {
public:
    MachTimOledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                       int height, bool mirror_x, bool mirror_y);
    virtual ~MachTimOledDisplay();

    virtual void SetEmotion(const char* emotion) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetMediaOverlayActive(bool active) override;

    /** Ve lai clip theo bo mat dang chon — goi sau khi doi MachTimEyeStyle. */
    void ReloadEyeStyle();

    /** Doc/ghi bo cuc man hinh. Ghi se luu vao NVS va co hieu luc ngay. */
    MachTimFaceLayout GetLayout() const { return layout_; }
    void SetLayout(MachTimFaceLayout layout);

private:
    static constexpr int kFaceW = 128;
    static constexpr int kFaceH = 48;
    static constexpr int kFrameBytes = (kFaceW * kFaceH + 7) / 8;
    static constexpr uint32_t kBlinkIntervalMs = 3500;
    static constexpr int kEyeScalePct = 80;  // LVGL scale: 256 = 100%
    static constexpr int kEyeScale = 256 * kEyeScalePct / 100;

    void SetupOttoFace();
    void CreateEyeCanvas();
    void LoadLayout();
    void ApplyLayout();
    /** true khi phai an mat de nhuong cho text kieu wechat. */
    bool TextMode() const { return layout_ == MachTimFaceLayout::kWechat && talking_; }
    void StartClip(const MachTimEyeClip& clip, bool restore_after);
    void DrawFrame(uint8_t index);
    void OnAnimTick();
    void OnBlinkTick();
    void RestoreEmotionClip();

    lv_obj_t* eye_container_ = nullptr;
    lv_obj_t* eye_canvas_ = nullptr;
    void* canvas_buffer_ = nullptr;

    lv_timer_t* anim_timer_ = nullptr;
    lv_timer_t* blink_timer_ = nullptr;

    char emotion_name_[24] = "staticstate";
    MachTimFaceLayout layout_ = MachTimFaceLayout::kFullFace;
    /* Giu lai cau chat gan nhat de khi doi sang wechat co san noi dung. */
    std::string chat_role_;
    std::string chat_content_;
    MachTimEyeClip emotion_clip_{};
    MachTimEyeClip active_clip_{};
    uint8_t frame_index_ = 0;
    bool restore_after_blink_ = false;
    bool blinking_ = false;
    bool talking_ = false;
};
