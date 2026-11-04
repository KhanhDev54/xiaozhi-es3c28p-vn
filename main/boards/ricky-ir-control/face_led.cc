#include "face_led.h"

#include "device_state_event.h"
#include "device_state.h"

#include <cstring>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "FACE_LED";

#define FACE_LED_COUNT_DEFAULT  8
#define FACE_RMT_RES_HZ         (10 * 1000 * 1000)
#define FACE_TASK_STACK         4096
#define FACE_TASK_PRIO          4
#define FACE_QUEUE_LEN          8
#define FACE_ANIM_TICK_MS       25

#define LED_LEFT_EYE    3
#define LED_RIGHT_EYE   0
#define LED_MOUTH_L     5
#define LED_MOUTH_R     6

/* BINH_THUONG */
#define IDLE_BLINK_MIN_MS       2500
#define IDLE_BLINK_MAX_MS       4000
#define IDLE_BLINK_CLOSE_MS     140
#define IDLE_MOUTH_INTERVAL_MS  500
#define IDLE_MOUTH_TUCK_MS      90

/* NOI_CHUYEN — miệng */
#define SPEAK_OPEN_MIN_MS       70
#define SPEAK_OPEN_MAX_MS       160
#define SPEAK_CLOSE_MIN_MS      45
#define SPEAK_CLOSE_MAX_MS      110

/* NOI_CHUYEN — mắt */
#define SPEAK_EYE_EVT_MIN_MS    3500
#define SPEAK_EYE_EVT_MAX_MS    7500
#define SPEAK_BLINK_FAST_MS     55
#define SPEAK_BLINK_SLOW_MS     280
#define SPEAK_BLINK_MID_MS      90
#define SPEAK_GLANCE_MS         600

typedef struct {
    uint8_t r, g, b;
} rgb_t;

typedef struct {
    rgb_t eye_l;
    rgb_t eye_r;
    bool mouth_l;
    bool mouth_r;
    rgb_t mouth_color;
} face_t;

typedef enum {
    MOUTH_OFF,
    MOUTH_NEUTRAL,
    MOUTH_SMILE,
    MOUTH_FROWN,
    MOUTH_OPEN,
    MOUTH_WINK_L,
    MOUTH_WINK_R,
} mouth_mode_t;

typedef enum {
    SPEAK_EYE_STARE,
    SPEAK_EYE_GLANCE_L,
    SPEAK_EYE_GLANCE_R,
} speak_eye_mode_t;

typedef struct {
    bool active;
    uint8_t beats;
    uint8_t step;
    int close_ms;
    int64_t step_until;
} speak_blink_t;

typedef struct {
    bool init;
    int64_t next_blink;
    int64_t next_mouth;
    bool tuck_left;
} idle_anim_t;

typedef struct {
    bool init;
    int64_t next_mouth;
    int64_t next_eye;
    int64_t eye_mode_until;
    bool mouth_open;
    speak_eye_mode_t eye_mode;
    speak_blink_t blink;
} speak_anim_t;

typedef struct {
    face_expr_t expr;
} face_cmd_t;

static const rgb_t COLOR_EYE_IDLE    = { 22, 22, 22 };
static const rgb_t COLOR_MOUTH_IDLE  = { 6, 6, 6 };
static const rgb_t COLOR_SPEAK_MOUTH = { 14, 14, 14 };
static const rgb_t SPEAK_EYE_BRIGHT  = { 26, 26, 26 };
static const rgb_t SPEAK_EYE_DIM     = { 6, 6, 6 };
static const rgb_t RGB_OFF           = { 0, 0, 0 };

static led_strip_handle_t s_strip = nullptr;
static QueueHandle_t s_queue = nullptr;
static face_expr_t s_expr = FACE_IDLE;
static volatile bool s_speaking = false;
static int s_led_count = FACE_LED_COUNT_DEFAULT;
static idle_anim_t s_idle_anim = {};
static speak_anim_t s_speak_anim = {};
static face_t s_speak_face = {};

static int64_t now_ms(void)
{
    return static_cast<int64_t>(esp_log_timestamp());
}

static int64_t random_ms(int min_ms, int max_ms)
{
    uint32_t span = static_cast<uint32_t>(max_ms - min_ms + 1);
    return min_ms + static_cast<int64_t>(esp_random() % span);
}

static bool rgb_is_on(rgb_t c)
{
    return c.r > 0 || c.g > 0 || c.b > 0;
}

static void render_face(const face_t *f)
{
    for (int i = 0; i < s_led_count; i++) {
        led_strip_set_pixel(s_strip, i, 0, 0, 0);
    }
    if (rgb_is_on(f->eye_l)) {
        led_strip_set_pixel(s_strip, LED_LEFT_EYE, f->eye_l.r, f->eye_l.g, f->eye_l.b);
    }
    if (rgb_is_on(f->eye_r)) {
        led_strip_set_pixel(s_strip, LED_RIGHT_EYE, f->eye_r.r, f->eye_r.g, f->eye_r.b);
    }
    if (f->mouth_l) {
        led_strip_set_pixel(s_strip, LED_MOUTH_L, f->mouth_color.r, f->mouth_color.g, f->mouth_color.b);
    }
    if (f->mouth_r) {
        led_strip_set_pixel(s_strip, LED_MOUTH_R, f->mouth_color.r, f->mouth_color.g, f->mouth_color.b);
    }
    led_strip_refresh(s_strip);
}

static void apply_mouth_legacy(mouth_mode_t mouth)
{
    rgb_t ml = {0, 0, 0};
    rgb_t mr = {0, 0, 0};

    switch (mouth) {
    case MOUTH_NEUTRAL:
        ml = {12, 12, 12};
        mr = {12, 12, 12};
        break;
    case MOUTH_SMILE:
        ml = {40, 45, 5};
        mr = {40, 45, 5};
        break;
    case MOUTH_FROWN:
        ml = {3, 3, 15};
        mr = {8, 2, 2};
        break;
    case MOUTH_OPEN:
        ml = {50, 15, 0};
        mr = {50, 15, 0};
        break;
    case MOUTH_WINK_L:
        ml = {35, 35, 8};
        mr = {8, 8, 8};
        break;
    case MOUTH_WINK_R:
        ml = {8, 8, 8};
        mr = {35, 35, 8};
        break;
    case MOUTH_OFF:
    default:
        break;
    }

    led_strip_set_pixel(s_strip, LED_MOUTH_L, ml.r, ml.g, ml.b);
    led_strip_set_pixel(s_strip, LED_MOUTH_R, mr.r, mr.g, mr.b);
}

static void face_render_legacy(rgb_t left, rgb_t right, mouth_mode_t mouth)
{
    for (int i = 0; i < s_led_count; i++) {
        led_strip_set_pixel(s_strip, i, 0, 0, 0);
    }
    led_strip_set_pixel(s_strip, LED_LEFT_EYE, left.r, left.g, left.b);
    led_strip_set_pixel(s_strip, LED_RIGHT_EYE, right.r, right.g, right.b);
    apply_mouth_legacy(mouth);
    led_strip_refresh(s_strip);
}

static void face_eyes_off_keep_mouth(mouth_mode_t mouth)
{
    led_strip_set_pixel(s_strip, LED_LEFT_EYE, 0, 0, 0);
    led_strip_set_pixel(s_strip, LED_RIGHT_EYE, 0, 0, 0);
    apply_mouth_legacy(mouth);
    led_strip_refresh(s_strip);
}

static void face_left_eye(rgb_t c, mouth_mode_t mouth)
{
    led_strip_set_pixel(s_strip, LED_LEFT_EYE, c.r, c.g, c.b);
    apply_mouth_legacy(mouth);
    led_strip_refresh(s_strip);
}

static void idle_anim_reset(void)
{
    s_idle_anim = {};
}

static void speak_anim_reset(void)
{
    s_speak_anim = {};
    s_speak_face = {
        .eye_l = SPEAK_EYE_BRIGHT,
        .eye_r = SPEAK_EYE_BRIGHT,
        .mouth_l = true,
        .mouth_r = true,
        .mouth_color = COLOR_SPEAK_MOUTH,
    };
}

static void apply_speak_eye_mode(face_t *f, speak_eye_mode_t mode)
{
    switch (mode) {
    case SPEAK_EYE_STARE:
        f->eye_l = SPEAK_EYE_BRIGHT;
        f->eye_r = SPEAK_EYE_BRIGHT;
        break;
    case SPEAK_EYE_GLANCE_L:
        f->eye_l = SPEAK_EYE_BRIGHT;
        f->eye_r = SPEAK_EYE_DIM;
        break;
    case SPEAK_EYE_GLANCE_R:
        f->eye_l = SPEAK_EYE_DIM;
        f->eye_r = SPEAK_EYE_BRIGHT;
        break;
    }
}

static void blink_seq_start(speak_blink_t *b, uint8_t beats, int close_ms)
{
    b->active = true;
    b->beats = beats;
    b->close_ms = close_ms;
    b->step = 0;
    b->step_until = now_ms();
}

static bool blink_seq_tick(speak_blink_t *b, face_t *f, int64_t now)
{
    if (!b->active || now < b->step_until) {
        return false;
    }

    switch (b->step) {
    case 0:
        f->eye_l = RGB_OFF;
        f->eye_r = RGB_OFF;
        b->step = 1;
        b->step_until = now + b->close_ms;
        break;
    case 1:
        f->eye_l = SPEAK_EYE_BRIGHT;
        f->eye_r = SPEAK_EYE_BRIGHT;
        if (b->beats == 1) {
            b->active = false;
        } else {
            b->step = 2;
            b->step_until = now + SPEAK_BLINK_MID_MS;
        }
        break;
    case 2:
        f->eye_l = RGB_OFF;
        f->eye_r = RGB_OFF;
        b->step = 3;
        b->step_until = now + b->close_ms;
        break;
    default:
        f->eye_l = SPEAK_EYE_BRIGHT;
        f->eye_r = SPEAK_EYE_BRIGHT;
        b->active = false;
        break;
    }
    return true;
}

static bool speak_eye_pick_event(speak_eye_mode_t *mode, int64_t *mode_until, speak_blink_t *blink)
{
    int roll = static_cast<int>(esp_random() % 100);
    int64_t now = now_ms();

    if (roll < 20) {
        *mode = SPEAK_EYE_STARE;
        *mode_until = 0;
        return false;
    }
    if (roll < 88) {
        uint8_t beats = (roll < 54) ? 1 : 2;
        int close_ms = ((esp_random() & 0x01) != 0) ? SPEAK_BLINK_FAST_MS : SPEAK_BLINK_SLOW_MS;
        blink_seq_start(blink, beats, close_ms);
        return true;
    }
    if (roll < 94) {
        *mode = SPEAK_EYE_GLANCE_L;
        *mode_until = now + SPEAK_GLANCE_MS;
        return false;
    }
    *mode = SPEAK_EYE_GLANCE_R;
    *mode_until = now + SPEAK_GLANCE_MS;
    return false;
}

static void speak_mouth_open(face_t *f)
{
    switch (esp_random() & 0x03) {
    case 0:
        f->mouth_l = true;
        f->mouth_r = true;
        break;
    case 1:
        f->mouth_l = false;
        f->mouth_r = false;
        break;
    case 2:
        f->mouth_l = true;
        f->mouth_r = false;
        break;
    default:
        f->mouth_l = false;
        f->mouth_r = true;
        break;
    }
    f->mouth_color = COLOR_SPEAK_MOUTH;
}

static bool tick_idle(void)
{
    int64_t now = now_ms();

    if (!s_idle_anim.init) {
        s_idle_anim.next_blink = now + random_ms(IDLE_BLINK_MIN_MS, IDLE_BLINK_MAX_MS);
        s_idle_anim.next_mouth = now + IDLE_MOUTH_INTERVAL_MS;
        s_idle_anim.tuck_left = true;
        s_idle_anim.init = true;
        face_t f = {
            .eye_l = COLOR_EYE_IDLE,
            .eye_r = COLOR_EYE_IDLE,
            .mouth_l = true,
            .mouth_r = true,
            .mouth_color = COLOR_MOUTH_IDLE,
        };
        render_face(&f);
        return true;
    }

    face_t f = {
        .eye_l = COLOR_EYE_IDLE,
        .eye_r = COLOR_EYE_IDLE,
        .mouth_l = true,
        .mouth_r = true,
        .mouth_color = COLOR_MOUTH_IDLE,
    };
    bool changed = false;

    if (now >= s_idle_anim.next_blink) {
        f.eye_l = RGB_OFF;
        f.eye_r = RGB_OFF;
        render_face(&f);
        vTaskDelay(pdMS_TO_TICKS(IDLE_BLINK_CLOSE_MS));
        f.eye_l = COLOR_EYE_IDLE;
        f.eye_r = COLOR_EYE_IDLE;
        render_face(&f);
        s_idle_anim.next_blink = now_ms() + random_ms(IDLE_BLINK_MIN_MS, IDLE_BLINK_MAX_MS);
        return true;
    }

    if (now >= s_idle_anim.next_mouth) {
        if (s_idle_anim.tuck_left) {
            f.mouth_l = false;
            f.mouth_r = true;
        } else {
            f.mouth_l = true;
            f.mouth_r = false;
        }
        render_face(&f);
        vTaskDelay(pdMS_TO_TICKS(IDLE_MOUTH_TUCK_MS));
        f.mouth_l = true;
        f.mouth_r = true;
        render_face(&f);
        s_idle_anim.tuck_left = !s_idle_anim.tuck_left;
        s_idle_anim.next_mouth = now_ms() + IDLE_MOUTH_INTERVAL_MS;
        return true;
    }

    return changed;
}

static bool tick_speaking(void)
{
    int64_t now = now_ms();

    if (!s_speak_anim.init) {
        s_speak_anim.next_mouth = now + random_ms(SPEAK_OPEN_MIN_MS, SPEAK_OPEN_MAX_MS);
        s_speak_anim.next_eye = now + random_ms(SPEAK_EYE_EVT_MIN_MS, SPEAK_EYE_EVT_MAX_MS);
        s_speak_anim.mouth_open = true;
        s_speak_anim.eye_mode = SPEAK_EYE_STARE;
        s_speak_anim.blink = {};
        s_speak_anim.init = true;
        s_speak_face = {
            .eye_l = SPEAK_EYE_BRIGHT,
            .eye_r = SPEAK_EYE_BRIGHT,
            .mouth_l = true,
            .mouth_r = true,
            .mouth_color = COLOR_SPEAK_MOUTH,
        };
        render_face(&s_speak_face);
        return true;
    }

    bool changed = false;

    if (s_speak_anim.blink.active) {
        if (blink_seq_tick(&s_speak_anim.blink, &s_speak_face, now)) {
            changed = true;
        }
        if (!s_speak_anim.blink.active) {
            s_speak_anim.eye_mode = SPEAK_EYE_STARE;
            s_speak_anim.next_eye = now + random_ms(SPEAK_EYE_EVT_MIN_MS, SPEAK_EYE_EVT_MAX_MS);
        }
    } else if (s_speak_anim.eye_mode_until > 0 && now >= s_speak_anim.eye_mode_until) {
        s_speak_anim.eye_mode = SPEAK_EYE_STARE;
        s_speak_anim.eye_mode_until = 0;
        apply_speak_eye_mode(&s_speak_face, s_speak_anim.eye_mode);
        s_speak_anim.next_eye = now + random_ms(SPEAK_EYE_EVT_MIN_MS, SPEAK_EYE_EVT_MAX_MS);
        changed = true;
    } else if (now >= s_speak_anim.next_eye) {
        if (speak_eye_pick_event(&s_speak_anim.eye_mode, &s_speak_anim.eye_mode_until, &s_speak_anim.blink)) {
            changed = blink_seq_tick(&s_speak_anim.blink, &s_speak_face, now);
        } else {
            apply_speak_eye_mode(&s_speak_face, s_speak_anim.eye_mode);
            if (s_speak_anim.eye_mode == SPEAK_EYE_STARE) {
                s_speak_anim.next_eye = now + random_ms(SPEAK_EYE_EVT_MIN_MS, SPEAK_EYE_EVT_MAX_MS);
            } else {
                s_speak_anim.next_eye = s_speak_anim.eye_mode_until +
                    random_ms(SPEAK_EYE_EVT_MIN_MS, SPEAK_EYE_EVT_MAX_MS);
            }
            changed = true;
        }
    }

    if (now >= s_speak_anim.next_mouth) {
        if (s_speak_anim.mouth_open) {
            s_speak_face.mouth_l = false;
            s_speak_face.mouth_r = false;
            s_speak_anim.mouth_open = false;
            s_speak_anim.next_mouth = now + random_ms(SPEAK_CLOSE_MIN_MS, SPEAK_CLOSE_MAX_MS);
        } else {
            speak_mouth_open(&s_speak_face);
            s_speak_anim.mouth_open = true;
            s_speak_anim.next_mouth = now + random_ms(SPEAK_OPEN_MIN_MS, SPEAK_OPEN_MAX_MS);
        }
        changed = true;
    }

    if (changed) {
        render_face(&s_speak_face);
    }

    return true;
}

static const char *expr_name(face_expr_t e)
{
    switch (e) {
    case FACE_IDLE: return "idle";
    case FACE_LISTENING: return "listening";
    case FACE_SPEAKING: return "speaking";
    case FACE_HAPPY: return "happy";
    case FACE_SAD: return "sad";
    case FACE_ANGRY: return "angry";
    case FACE_SURPRISED: return "surprised";
    case FACE_THINKING: return "thinking";
    case FACE_SLEEP: return "sleep";
    case FACE_WINK: return "wink";
    default: return "?";
    }
}

static void on_device_state(DeviceState previous, DeviceState current)
{
    (void)previous;
    switch (current) {
    case kDeviceStateIdle:
        face_led_set(FACE_IDLE);
        break;
    case kDeviceStateListening:
    case kDeviceStateAudioTesting:
        face_led_set(FACE_LISTENING);
        break;
    case kDeviceStateSpeaking:
        face_led_set(FACE_SPEAKING);
        break;
    default:
        break;
    }
}

static bool tick_ngu(void)
{
    face_render_legacy({2, 2, 4}, {2, 2, 4}, MOUTH_OFF);
    return false;
}

static bool tick_vui(void)
{
    face_render_legacy({0, 55, 10}, {0, 55, 10}, MOUTH_SMILE);
    return false;
}

static bool tick_buon(void)
{
    face_render_legacy({0, 5, 30}, {0, 5, 30}, MOUTH_FROWN);
    return false;
}

static bool tick_sung_sot(void)
{
    face_render_legacy({50, 0, 0}, {0, 50, 0}, MOUTH_OPEN);
    return false;
}

static bool tick_listening(int phase)
{
    rgb_t bright = COLOR_EYE_IDLE;
    rgb_t dim = SPEAK_EYE_DIM;
    if ((phase & 1) == 0) {
        face_render_legacy(bright, dim, MOUTH_NEUTRAL);
    } else {
        face_render_legacy(dim, bright, MOUTH_NEUTRAL);
    }
    return true;
}

static bool tick_angry(int phase)
{
    rgb_t alert = {55, 0, 0};
    if ((phase & 1) == 0) {
        face_render_legacy(alert, alert, MOUTH_OPEN);
    } else {
        face_eyes_off_keep_mouth(MOUTH_OFF);
    }
    return true;
}

static bool tick_wink(int phase)
{
    rgb_t left = COLOR_EYE_IDLE;
    rgb_t right = SPEAK_EYE_DIM;
    if ((phase & 1) == 0) {
        face_render_legacy(left, right, MOUTH_WINK_L);
        face_left_eye(left, MOUTH_WINK_L);
    } else {
        face_left_eye(RGB_OFF, MOUTH_WINK_L);
    }
    return true;
}

static void face_task(void *arg)
{
    (void)arg;
    int anim_phase = 0;
    face_expr_t active = FACE_IDLE;
    int tick_ms = FACE_ANIM_TICK_MS;

    idle_anim_reset();
    speak_anim_reset();
    face_t boot = {
        .eye_l = COLOR_EYE_IDLE,
        .eye_r = COLOR_EYE_IDLE,
        .mouth_l = true,
        .mouth_r = true,
        .mouth_color = COLOR_MOUTH_IDLE,
    };
    render_face(&boot);
    ESP_LOGI(TAG, "Face LED task started (idle/speaking eyes ported)");

    while (true) {
        face_cmd_t cmd;
        if (xQueueReceive(s_queue, &cmd, pdMS_TO_TICKS(tick_ms)) == pdTRUE) {
            if (cmd.expr != active) {
                ESP_LOGI(TAG, "Expression: %s -> %s", expr_name(active), expr_name(cmd.expr));
                if (active == FACE_IDLE || cmd.expr == FACE_IDLE) {
                    idle_anim_reset();
                }
                if (active == FACE_SPEAKING || cmd.expr == FACE_SPEAKING) {
                    speak_anim_reset();
                }
                active = cmd.expr;
                anim_phase = 0;
                s_expr = active;
            }
        }

        s_speaking = (active == FACE_SPEAKING);
        bool animated = false;

        switch (active) {
        case FACE_IDLE:
            tick_ms = FACE_ANIM_TICK_MS;
            animated = tick_idle();
            break;
        case FACE_SLEEP:
            tick_ms = 800;
            tick_ngu();
            break;
        case FACE_LISTENING:
            tick_ms = 400;
            animated = tick_listening(anim_phase++);
            break;
        case FACE_SPEAKING:
            tick_ms = FACE_ANIM_TICK_MS;
            animated = tick_speaking();
            break;
        case FACE_HAPPY:
            tick_ms = 300;
            tick_vui();
            break;
        case FACE_SAD:
            tick_ms = 400;
            tick_buon();
            break;
        case FACE_ANGRY:
            tick_ms = 120;
            animated = tick_angry(anim_phase++);
            break;
        case FACE_SURPRISED:
            tick_ms = 300;
            tick_sung_sot();
            break;
        case FACE_THINKING:
            tick_ms = 400;
            animated = tick_listening(anim_phase++);
            break;
        case FACE_WINK:
            tick_ms = 250;
            animated = tick_wink(anim_phase++);
            break;
        default:
            tick_ms = FACE_ANIM_TICK_MS;
            animated = tick_idle();
            break;
        }

        if (!animated) {
            anim_phase = 0;
        }
    }
}

void face_led_set(face_expr_t expr)
{
    if (!s_queue) {
        s_expr = expr;
        return;
    }
    face_cmd_t cmd = {.expr = expr};
    xQueueSend(s_queue, &cmd, 0);
}

void face_led_set_from_emotion(const char *emotion)
{
    if (!emotion || s_speaking) {
        return;
    }

    if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "laughing") == 0 ||
        strcmp(emotion, "funny") == 0 || strcmp(emotion, "loving") == 0 ||
        strcmp(emotion, "confident") == 0 || strcmp(emotion, "cool") == 0 ||
        strcmp(emotion, "delicious") == 0 || strcmp(emotion, "kissy") == 0 ||
        strcmp(emotion, "silly") == 0) {
        face_led_set(FACE_HAPPY);
    } else if (strcmp(emotion, "sad") == 0 || strcmp(emotion, "crying") == 0) {
        face_led_set(FACE_SAD);
    } else if (strcmp(emotion, "angry") == 0 || strcmp(emotion, "anger") == 0) {
        face_led_set(FACE_ANGRY);
    } else if (strcmp(emotion, "surprised") == 0 || strcmp(emotion, "shocked") == 0) {
        face_led_set(FACE_SURPRISED);
    } else if (strcmp(emotion, "winking") == 0) {
        face_led_set(FACE_WINK);
    } else if (strcmp(emotion, "thinking") == 0 || strcmp(emotion, "confused") == 0 ||
               strcmp(emotion, "embarrassed") == 0) {
        face_led_set(FACE_THINKING);
    } else if (strcmp(emotion, "sleepy") == 0 || strcmp(emotion, "sleeping") == 0) {
        face_led_set(FACE_SLEEP);
    } else if (strcmp(emotion, "neutral") == 0 || strcmp(emotion, "idle") == 0 ||
               strcmp(emotion, "relaxed") == 0) {
        face_led_set(FACE_IDLE);
    }
}

void face_led_init(gpio_num_t gpio, int led_count)
{
    if (s_strip) {
        return;
    }

    s_led_count = led_count > 0 ? led_count : FACE_LED_COUNT_DEFAULT;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = gpio,
        .max_leds = static_cast<uint32_t>(s_led_count),
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {.invert_out = false},
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = FACE_RMT_RES_HZ,
        .mem_block_symbols = 0,
        .flags = {.with_dma = false},
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    ESP_LOGI(TAG, "WS2812 face on GPIO %d (%d LEDs)", static_cast<int>(gpio), s_led_count);

    s_queue = xQueueCreate(FACE_QUEUE_LEN, sizeof(face_cmd_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create face_led queue");
        return;
    }
    if (xTaskCreate(face_task, "face_led", FACE_TASK_STACK, nullptr, FACE_TASK_PRIO, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create face_led task");
        return;
    }

    DeviceStateEventManager::GetInstance().RegisterStateChangeCallback(on_device_state);
    face_led_set(s_expr);
}
