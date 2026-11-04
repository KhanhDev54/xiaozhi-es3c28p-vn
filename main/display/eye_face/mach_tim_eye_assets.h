#pragma once

#include <cstdint>
#include <cstring>

struct MachTimEyeClip {
    const uint8_t* const* frames;
    uint8_t frame_count;
    uint8_t fps;
    bool loop;
};

// Clip symbols (defined in assets/eyes_*.c)
extern const uint8_t* const happy_loop_frames[];
extern const uint8_t happy_loop_frame_count;
extern const uint8_t* const sad_loop_frames[];
extern const uint8_t sad_loop_frame_count;
extern const uint8_t* const anger_loop_frames[];
extern const uint8_t anger_loop_frame_count;
extern const uint8_t* const panic_loop_frames[];
extern const uint8_t panic_loop_frame_count;
extern const uint8_t* const scorn_loop_frames[];
extern const uint8_t scorn_loop_frame_count;
extern const uint8_t* const blink_quick_frames[];
extern const uint8_t blink_quick_frame_count;

// Bo mat pika — defined in assets/eyes_pika_*.c
extern const uint8_t* const pika_neutral_frames[];
extern const uint8_t pika_neutral_frame_count;
extern const uint8_t* const pika_talk_frames[];
extern const uint8_t pika_talk_frame_count;
extern const uint8_t* const pika_blink_frames[];
extern const uint8_t pika_blink_frame_count;

enum class MachTimEyeStyle {
    kOtto = 0, // bo mat goc
    kPika = 1, // mat pika
};

/** Doc/ghi bo mat dang dung. Ghi se luu vao NVS. */
MachTimEyeStyle MachTimEyeStyleGet();
bool MachTimEyeStyleSet(MachTimEyeStyle style);
const char* MachTimEyeStyleName(MachTimEyeStyle style);
bool MachTimEyeStyleParse(const char* name, MachTimEyeStyle* out);

MachTimEyeClip MachTimEyeClipForEmotion(const char* emotion);
const MachTimEyeClip* MachTimEyeClipBlinkQuick();

/** Clip dung khi thiet bi dang noi (mieng map may). */
MachTimEyeClip MachTimEyeClipTalking();
