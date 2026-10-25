// Face renderer. Portable C with no platform deps: runs in the phone app (wasm) and on the ESP32.
#pragma once
#include <stdint.h>

#define EYE_SIZE 360  // 360x360 round LCD (Waveshare ESP32-S3-LCD-1.85)

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

void eye_init(uint32_t seed);
void eye_gaze(float x, float y);  // -1..1, where the cursor is relative to screen center
void eye_set_expression(expr_t ex, float intensity);
void eye_blink(void);
void eye_set_listening(int on);
void eye_update(uint32_t now_ms);
void eye_render(uint16_t *fb);    // fills EYE_SIZE*EYE_SIZE RGB565 pixels
