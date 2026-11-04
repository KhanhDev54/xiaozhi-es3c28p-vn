#include "mach_tim_eye_assets.h"

#include <strings.h>

#include "esp_log.h"
#include "nvs.h"

#define TAG "MachTimEyeStyle"

namespace {

const char* kNvsNamespace = "face";
const char* kNvsKey = "eye_style";

MachTimEyeClip MakeClip(const uint8_t* const* frames, uint8_t count, uint8_t fps, bool loop) {
    return {frames, count, fps, loop};
}

const MachTimEyeClip kHappy = MakeClip(happy_loop_frames, happy_loop_frame_count, 25, true);
const MachTimEyeClip kSad = MakeClip(sad_loop_frames, sad_loop_frame_count, 25, true);
const MachTimEyeClip kAnger = MakeClip(anger_loop_frames, anger_loop_frame_count, 25, true);
const MachTimEyeClip kPanic = MakeClip(panic_loop_frames, panic_loop_frame_count, 25, true);
const MachTimEyeClip kScorn = MakeClip(scorn_loop_frames, scorn_loop_frame_count, 25, true);
const MachTimEyeClip kBlink = MakeClip(blink_quick_frames, blink_quick_frame_count, 5, false);

/* Hai clip duoc lay mau lai ve 8fps trong tools/generate_pika_eyes.py.
   Blink cat truc tiep tu frame goc 60ms nen phat ~16fps. */
const MachTimEyeClip kPikaNeutral =
    MakeClip(pika_neutral_frames, pika_neutral_frame_count, 8, true);
const MachTimEyeClip kPikaTalk = MakeClip(pika_talk_frames, pika_talk_frame_count, 8, true);
const MachTimEyeClip kPikaBlink = MakeClip(pika_blink_frames, pika_blink_frame_count, 16, false);

/* Mac dinh mat pika; doi qua self.face.set_eye_style va luu NVS. */
MachTimEyeStyle s_style = MachTimEyeStyle::kPika;
bool s_style_loaded = false;

bool Eq(const char* a, const char* b) {
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

void LoadStyle() {
    if (s_style_loaded) {
        return;
    }
    s_style_loaded = true;

    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
        int32_t stored = 0;
        if (nvs_get_i32(handle, kNvsKey, &stored) == ESP_OK &&
            (stored == static_cast<int32_t>(MachTimEyeStyle::kOtto) ||
             stored == static_cast<int32_t>(MachTimEyeStyle::kPika))) {
            s_style = static_cast<MachTimEyeStyle>(stored);
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Eye style: %s", MachTimEyeStyleName(s_style));
}

MachTimEyeClip PikaClipForEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return kPikaNeutral;
    }
    if (Eq(emotion, "winking")) {
        return kPikaBlink;
    }
    /* Cac cam xuc vui dung clip co mieng map may. */
    if (Eq(emotion, "happy") || Eq(emotion, "laughing") || Eq(emotion, "funny") ||
        Eq(emotion, "loving") || Eq(emotion, "delicious") || Eq(emotion, "kissy") ||
        Eq(emotion, "silly") || Eq(emotion, "confident") || Eq(emotion, "cool")) {
        return kPikaTalk;
    }
    return kPikaNeutral;
}

}  // namespace

MachTimEyeStyle MachTimEyeStyleGet() {
    LoadStyle();
    return s_style;
}

bool MachTimEyeStyleSet(MachTimEyeStyle style) {
    if (style != MachTimEyeStyle::kOtto && style != MachTimEyeStyle::kPika) {
        return false;
    }
    s_style = style;
    s_style_loaded = true;

    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        if (nvs_set_i32(handle, kNvsKey, static_cast<int32_t>(style)) == ESP_OK) {
            nvs_commit(handle);
        }
        nvs_close(handle);
    } else {
        ESP_LOGW(TAG, "NVS open failed — eye style not persisted");
    }
    ESP_LOGI(TAG, "Eye style set to: %s", MachTimEyeStyleName(style));
    return true;
}

const char* MachTimEyeStyleName(MachTimEyeStyle style) {
    switch (style) {
    case MachTimEyeStyle::kOtto:
        return "otto";
    case MachTimEyeStyle::kPika:
        return "pika";
    default:
        return "unknown";
    }
}

bool MachTimEyeStyleParse(const char* name, MachTimEyeStyle* out) {
    if (name == nullptr || out == nullptr) {
        return false;
    }
    if (strcasecmp(name, "otto") == 0 || strcasecmp(name, "default") == 0 ||
        strcasecmp(name, "classic") == 0) {
        *out = MachTimEyeStyle::kOtto;
        return true;
    }
    if (strcasecmp(name, "pika") == 0 || strcasecmp(name, "pikachu") == 0) {
        *out = MachTimEyeStyle::kPika;
        return true;
    }
    return false;
}

MachTimEyeClip MachTimEyeClipForEmotion(const char* emotion) {
    if (MachTimEyeStyleGet() == MachTimEyeStyle::kPika) {
        return PikaClipForEmotion(emotion);
    }
    if (emotion == nullptr) {
        return kHappy;
    }
    if (Eq(emotion, "happy") || Eq(emotion, "laughing") || Eq(emotion, "funny") ||
        Eq(emotion, "loving") || Eq(emotion, "embarrassed") || Eq(emotion, "confident") ||
        Eq(emotion, "delicious") || Eq(emotion, "cool") || Eq(emotion, "kissy") ||
        Eq(emotion, "silly")) {
        return kHappy;
    }
    if (Eq(emotion, "sad") || Eq(emotion, "crying") || Eq(emotion, "sleepy")) {
        return kSad;
    }
    if (Eq(emotion, "angry") || Eq(emotion, "anger")) {
        return kAnger;
    }
    if (Eq(emotion, "surprised") || Eq(emotion, "shocked") || Eq(emotion, "scare")) {
        return kPanic;
    }
    if (Eq(emotion, "thinking") || Eq(emotion, "confused") || Eq(emotion, "buxue") ||
        Eq(emotion, "relaxed")) {
        return kScorn;
    }
    if (Eq(emotion, "winking")) {
        return kBlink;
    }
    if (Eq(emotion, "staticstate") || Eq(emotion, "neutral") || Eq(emotion, "idle") ||
        Eq(emotion, "microchip_ai")) {
        // Static neutral: single frame from happy_loop
        static const MachTimEyeClip kNeutral = {&happy_loop_frames[0], 1, 1, false};
        return kNeutral;
    }
    return kHappy;
}

const MachTimEyeClip* MachTimEyeClipBlinkQuick() {
    return MachTimEyeStyleGet() == MachTimEyeStyle::kPika ? &kPikaBlink : &kBlink;
}

MachTimEyeClip MachTimEyeClipTalking() {
    return MachTimEyeStyleGet() == MachTimEyeStyle::kPika ? kPikaTalk : kHappy;
}
