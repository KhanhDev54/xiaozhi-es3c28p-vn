#include "mach_tim_oled_display.h"

#include <cstring>

#include <strings.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <nvs.h>

#include "assets/lang_config.h"

#define TAG "MachTimOled"

namespace {
const char* kNvsNamespace = "face";
const char* kNvsLayoutKey = "layout";
}  // namespace

const char* MachTimFaceLayoutName(MachTimFaceLayout layout) {
    return layout == MachTimFaceLayout::kWechat ? "wechat" : "face";
}

bool MachTimFaceLayoutParse(const char* name, MachTimFaceLayout* out) {
    if (name == nullptr || out == nullptr) {
        return false;
    }
    if (strcasecmp(name, "wechat") == 0 || strcasecmp(name, "chat") == 0 ||
        strcasecmp(name, "text") == 0) {
        *out = MachTimFaceLayout::kWechat;
        return true;
    }
    if (strcasecmp(name, "face") == 0 || strcasecmp(name, "mat") == 0 ||
        strcasecmp(name, "default") == 0) {
        *out = MachTimFaceLayout::kFullFace;
        return true;
    }
    return false;
}

MachTimOledDisplay::MachTimOledDisplay(esp_lcd_panel_io_handle_t panel_io,
                                        esp_lcd_panel_handle_t panel, int width, int height,
                                        bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {
    LoadLayout();
    SetupOttoFace();
    emotion_clip_ = MachTimEyeClipForEmotion("staticstate");
    StartClip(emotion_clip_, false);
}

MachTimOledDisplay::~MachTimOledDisplay() {
    if (anim_timer_ != nullptr) {
        lv_timer_delete(anim_timer_);
        anim_timer_ = nullptr;
    }
    if (blink_timer_ != nullptr) {
        lv_timer_delete(blink_timer_);
        blink_timer_ = nullptr;
    }
    if (canvas_buffer_ != nullptr) {
        heap_caps_free(canvas_buffer_);
        canvas_buffer_ = nullptr;
    }
}

void MachTimOledDisplay::SetupOttoFace() {
    DisplayLockGuard lock(this);
    if (container_ == nullptr) {
        ESP_LOGE(TAG, "SetupOttoFace: container_ null");
        return;
    }

    if (emotion_label_ != nullptr) {
        lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (content_left_ != nullptr) {
        lv_obj_add_flag(content_left_, LV_OBJ_FLAG_HIDDEN);
    }
    /* Mac dinh mat che kin man hinh nen text chi lam ton CPU va bang thong I2C. */
    if (content_right_ != nullptr) {
        lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
    }
    if (battery_label_ != nullptr) {
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (low_battery_popup_ != nullptr) {
        lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
    }

    constexpr int kStatusBarH = 16;
    const int face_h = height_ - kStatusBarH;

    eye_container_ = lv_obj_create(container_);
    lv_obj_set_size(eye_container_, width_, face_h);
    lv_obj_set_pos(eye_container_, 0, kStatusBarH);
    lv_obj_add_flag(eye_container_, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_bg_opa(eye_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(eye_container_, 0, 0);
    lv_obj_set_style_pad_all(eye_container_, 0, 0);
    lv_obj_remove_flag(eye_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(eye_container_);

    CreateEyeCanvas();

    blink_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* self = static_cast<MachTimOledDisplay*>(lv_timer_get_user_data(timer));
            self->OnBlinkTick();
        },
        kBlinkIntervalMs, this);

    ESP_LOGI(TAG, "Face canvas %dx%d at y=%d (scale=%d%%)", kFaceW, kFaceH, kStatusBarH,
             kEyeScalePct);
}

void MachTimOledDisplay::LoadLayout() {
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        int32_t stored = 0;
        if (nvs_get_i32(handle, kNvsLayoutKey, &stored) == ESP_OK &&
            stored == static_cast<int32_t>(MachTimFaceLayout::kWechat)) {
            layout_ = MachTimFaceLayout::kWechat;
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Layout: %s", MachTimFaceLayoutName(layout_));
}

void MachTimOledDisplay::SetLayout(MachTimFaceLayout layout) {
    if (layout != layout_) {
        layout_ = layout;

        nvs_handle_t handle;
        if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
            if (nvs_set_i32(handle, kNvsLayoutKey, static_cast<int32_t>(layout)) == ESP_OK) {
                nvs_commit(handle);
            }
            nvs_close(handle);
        } else {
            ESP_LOGW(TAG, "NVS open failed — layout not persisted");
        }

        /* Che do mat khong day text xuong label, nen phai bu lai khi doi sang wechat. */
        if (layout_ == MachTimFaceLayout::kWechat && !chat_content_.empty()) {
            OledDisplay::SetChatMessage(chat_role_.c_str(), chat_content_.c_str());
        }
    }

    ApplyLayout();
    ESP_LOGI(TAG, "Layout set to: %s", MachTimFaceLayoutName(layout_));
}

void MachTimOledDisplay::ApplyLayout() {
    DisplayLockGuard lock(this);

    /* Overlay nhac/QR da chiem vung duoi status bar, de no tu quan ly widget. */
    if (media_overlay_active_) {
        return;
    }

    const bool text_mode = TextMode();

    if (content_left_ != nullptr) {
        if (text_mode) {
            lv_obj_remove_flag(content_left_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(content_left_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (emotion_label_ != nullptr) {
        if (text_mode) {
            lv_obj_remove_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (content_right_ != nullptr) {
        if (text_mode && !chat_content_.empty()) {
            lv_obj_remove_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (eye_container_ == nullptr) {
        return;
    }
    if (text_mode) {
        lv_obj_add_flag(eye_container_, LV_OBJ_FLAG_HIDDEN);
        /* Canvas bi an thi khong can ve tiep. */
        if (anim_timer_ != nullptr) {
            lv_timer_delete(anim_timer_);
            anim_timer_ = nullptr;
        }
    } else {
        lv_obj_remove_flag(eye_container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(eye_container_);
        if (anim_timer_ == nullptr) {
            blinking_ = false;
            restore_after_blink_ = false;
            StartClip(talking_ ? MachTimEyeClipTalking() : emotion_clip_, false);
        }
    }
}

void MachTimOledDisplay::CreateEyeCanvas() {
    if (eye_container_ == nullptr) {
        return;
    }

    const size_t buf_size = LV_DRAW_BUF_SIZE(kFaceW, kFaceH, LV_COLOR_FORMAT_I1);
    canvas_buffer_ = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (canvas_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate eye canvas buffer");
        return;
    }
    memset(canvas_buffer_, 0, buf_size);

    eye_canvas_ = lv_canvas_create(eye_container_);
    lv_canvas_set_buffer(eye_canvas_, canvas_buffer_, kFaceW, kFaceH, LV_COLOR_FORMAT_I1);
    lv_obj_set_size(eye_canvas_, kFaceW, kFaceH);
    lv_obj_center(eye_canvas_);

    lv_canvas_set_palette(eye_canvas_, 1, lv_color_to_32(lv_color_black(), LV_OPA_COVER));
    lv_canvas_set_palette(eye_canvas_, 0, lv_color_to_32(lv_color_white(), LV_OPA_COVER));
    lv_canvas_fill_bg(eye_canvas_, lv_color_white(), LV_OPA_COVER);

    lv_obj_set_style_transform_pivot_x(eye_canvas_, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(eye_canvas_, lv_pct(50), 0);
    lv_obj_set_style_transform_scale_x(eye_canvas_, kEyeScale, 0);
    lv_obj_set_style_transform_scale_y(eye_canvas_, kEyeScale, 0);

    lv_obj_move_foreground(eye_canvas_);
}

void MachTimOledDisplay::DrawFrame(uint8_t index) {
    if (eye_canvas_ == nullptr || canvas_buffer_ == nullptr || active_clip_.frames == nullptr) {
        return;
    }
    if (index >= active_clip_.frame_count) {
        index = 0;
    }

    const uint8_t* src = active_clip_.frames[index];
    uint8_t* dst = static_cast<uint8_t*>(canvas_buffer_) + 8;  // skip I1 palette
    memcpy(dst, src, kFrameBytes);
    lv_obj_invalidate(eye_canvas_);
}

void MachTimOledDisplay::StartClip(const MachTimEyeClip& clip, bool restore_after) {
    DisplayLockGuard lock(this);

    active_clip_ = clip;
    frame_index_ = 0;
    restore_after_blink_ = restore_after;

    DrawFrame(0);

    if (anim_timer_ != nullptr) {
        lv_timer_delete(anim_timer_);
        anim_timer_ = nullptr;
    }

    if (clip.frame_count <= 1 && !clip.loop) {
        return;
    }

    const uint32_t period = clip.fps > 0 ? (1000 / clip.fps) : 40;
    anim_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* self = static_cast<MachTimOledDisplay*>(lv_timer_get_user_data(timer));
            self->OnAnimTick();
        },
        period, this);
}

void MachTimOledDisplay::OnAnimTick() {
    if (active_clip_.frames == nullptr || active_clip_.frame_count == 0) {
        return;
    }

    DisplayLockGuard lock(this);
    frame_index_++;

    if (frame_index_ >= active_clip_.frame_count) {
        if (active_clip_.loop) {
            frame_index_ = 0;
        } else {
            frame_index_ = active_clip_.frame_count - 1;
            if (anim_timer_ != nullptr) {
                lv_timer_delete(anim_timer_);
                anim_timer_ = nullptr;
            }
            if (restore_after_blink_) {
                restore_after_blink_ = false;
                blinking_ = false;
                active_clip_ = emotion_clip_;
                frame_index_ = 0;
                DrawFrame(0);
                if (emotion_clip_.frame_count > 1 || emotion_clip_.loop) {
                    const uint32_t period = emotion_clip_.fps > 0 ? (1000 / emotion_clip_.fps) : 40;
                    anim_timer_ = lv_timer_create(
                        [](lv_timer_t* timer) {
                            auto* self = static_cast<MachTimOledDisplay*>(lv_timer_get_user_data(timer));
                            self->OnAnimTick();
                        },
                        period, this);
                }
            }
            return;
        }
    }

    DrawFrame(frame_index_);
}

void MachTimOledDisplay::OnBlinkTick() {
    /* Khi dang noi thi clip mieng map may da co nhay mat san, khong chen them. */
    if (blinking_ || talking_ || emotion_clip_.frame_count == 0) {
        return;
    }
    const MachTimEyeClip* blink = MachTimEyeClipBlinkQuick();
    if (blink == nullptr || blink->frame_count == 0) {
        return;
    }
    blinking_ = true;
    StartClip(*blink, true);
}

void MachTimOledDisplay::RestoreEmotionClip() {
    StartClip(emotion_clip_, false);
}

void MachTimOledDisplay::SetEmotion(const char* emotion) {
    const char* name = emotion != nullptr ? emotion : "staticstate";
    strncpy(emotion_name_, name, sizeof(emotion_name_) - 1);
    emotion_name_[sizeof(emotion_name_) - 1] = '\0';

    /* Cap nhat icon mat nho (font awesome) dung cho bo cuc wechat. */
    OledDisplay::SetEmotion(emotion_name_);

    emotion_clip_ = MachTimEyeClipForEmotion(emotion_name_);
    blinking_ = false;
    restore_after_blink_ = false;
    /* Dang noi thi giu clip mieng map may, chi nho cam xuc de dung lai sau. */
    if (!talking_) {
        StartClip(emotion_clip_, false);
    }
    ApplyLayout();
    ESP_LOGI(TAG, "Eyes [%s]: %s (frames=%u fps=%u)",
             MachTimEyeStyleName(MachTimEyeStyleGet()), emotion_name_,
             emotion_clip_.frame_count, emotion_clip_.fps);
}

void MachTimOledDisplay::ReloadEyeStyle() {
    SetEmotion(emotion_name_);
}

void MachTimOledDisplay::SetStatus(const char* status) {
    LvglDisplay::SetStatus(status);

    /* Trang thai la hook duy nhat cho biet thiet bi dang noi ma khong phai sua
       Application, vi kDeviceStateSpeaking khong goi SetEmotion. */
    const bool speaking = status != nullptr && strcmp(status, Lang::Strings::SPEAKING) == 0;
    if (speaking == talking_) {
        return;
    }
    talking_ = speaking;

    blinking_ = false;
    restore_after_blink_ = false;
    if (!TextMode()) {
        StartClip(speaking ? MachTimEyeClipTalking() : emotion_clip_, false);
    }
    ApplyLayout();
    ESP_LOGI(TAG, "Eyes %s (%s)", speaking ? "talking" : "idle/listening",
             MachTimFaceLayoutName(layout_));
}

void MachTimOledDisplay::SetChatMessage(const char* role, const char* content) {
    chat_role_ = role != nullptr ? role : "";
    chat_content_ = content != nullptr ? content : "";

    /* Che do mat: khong ve text nua vi mat che kin, do lai CPU cho animation. */
    if (layout_ == MachTimFaceLayout::kWechat) {
        OledDisplay::SetChatMessage(role, content);
    }
    ApplyLayout();
}

void MachTimOledDisplay::SetMediaOverlayActive(bool active) {
    OledDisplay::SetMediaOverlayActive(active);

    {
        DisplayLockGuard lock(this);
        if (eye_container_ != nullptr && active) {
            lv_obj_add_flag(eye_container_, LV_OBJ_FLAG_HIDDEN);
            if (anim_timer_ != nullptr) {
                lv_timer_pause(anim_timer_);
            }
            if (blink_timer_ != nullptr) {
                lv_timer_pause(blink_timer_);
            }
        }
    }

    if (!active) {
        ApplyLayout();
        DisplayLockGuard lock(this);
        if (anim_timer_ != nullptr) {
            lv_timer_resume(anim_timer_);
        }
        if (blink_timer_ != nullptr) {
            lv_timer_resume(blink_timer_);
        }
    }
}
