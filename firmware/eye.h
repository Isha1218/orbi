// Face renderer. Portable C with no platform deps: runs in the phone app (wasm) and on the ESP32.
#pragma once
#include <stdint.h>

#define EYE_SIZE 360  // 360x360 round LCD (Waveshare ESP32-S3-LCD-1.85)
#define AVATAR_SIZE 120  // avatar images are 120x120 RGB565

typedef enum {
    EXPR_IDLE,
    EXPR_LISTENING,
    EXPR_THINKING,
    EXPR_BUSY,
    EXPR_PLEASED,
    EXPR_WORRIED,
    EXPR_EXPECTANT,
    EXPR_CONFUSED,
    EXPR_SLEEPY,
    EXPR_COUNT
} expr_t;

typedef enum {
    SCENE_RAIN,
    SCENE_SNOW,
    SCENE_SUN,
    SCENE_STORM,
    SCENE_CLOUD,
    SCENE_CONFETTI,
    SCENE_SPARKLES,
    SCENE_ZZZ,
    SCENE_COUNT
} scene_t;

typedef enum {
    MOOD_NEUTRAL,
    MOOD_HAPPY,
    MOOD_GRUMPY,
    MOOD_COUNT
} mood_t;

typedef enum {
    ICON_MAIL,
    ICON_CALENDAR,
    ICON_CHECK,
    ICON_CROSS,
    ICON_WARNING,
    ICON_CLOCK,
    ICON_BELL,
    ICON_CHAT,
    ICON_CODE,
    ICON_MUSIC,
    ICON_STAR,
    ICON_HEART,
    ICON_COUNT
} icon_t;

void eye_init(uint32_t seed);
void eye_gaze(float x, float y);  // -1..1, where the cursor is relative to screen center
void eye_set_expression(expr_t ex, float intensity);
void eye_blink(void);
void eye_set_listening(int on);
void eye_update(uint32_t now_ms);
void eye_render(uint16_t *fb);    // fills EYE_SIZE*EYE_SIZE RGB565 pixels
void eye_render_rows(uint16_t *buf, int y0, int y1);  // rows y0..y1-1, EYE_SIZE pixels per row

// Scenes and glance cards show for duration_ms, then the eyes come back. label may be NULL.
// A new scene or card replaces the one showing.
void eye_scene(scene_t scene, mood_t mood, const char *label, uint32_t duration_ms);
void eye_card_icon(icon_t icon, const char *label, uint32_t duration_ms);
void eye_card_initials(const char *initials, const char *label, uint32_t duration_ms);
void eye_card_avatar(const char *label, uint32_t duration_ms);
void eye_set_avatar(const uint16_t *px);  // AVATAR_SIZE*AVATAR_SIZE RGB565 pixels, copied
