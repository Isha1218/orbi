// Minimal glowing white eyes on a black glass ball. Lids and eye shape carry the mood;
// expressions morph into each other instead of snapping.
#include "eye.h"
#include <math.h>
#include <string.h>

typedef struct { float r, g, b; } rgb_t;

#define DONE_SECONDS 4.0f   // "done" celebrates, then returns to idle
#define MORPH_SECONDS 0.3f  // expression change duration
#define FACE_SCALE 1.2f     // size of the whole face on the ball
#define AA 0.012f           // edge softness, about one pixel in face units
#define EYE_HW (0.05f + 0.035f * e.blink)  // eye half-width: widens as it blinks (squash and stretch)

static struct {
    eye_state_t state, prev;
    uint32_t state_since, now, last, next_blink, blink_start, rng;
    uint32_t moved_at, next_glance;
    float look_x, look_y;   // target from the cursor
    float glance_x, glance_y;  // where it looks on its own when the cursor is still
    float gaze_x, gaze_y;   // smoothed: where the face points
    float blink;            // 0 open .. 1 closed
    float morph;            // 0 = showing prev expression .. 1 = fully current
} e;

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float smoothstep(float a, float b, float x) { x = clampf((x - a) / (b - a), 0, 1); return x * x * (3 - 2 * x); }
static float approach(float cur, float target, float k) { return cur + (target - cur) * k; }
static uint32_t rnd(void) { e.rng = e.rng * 1664525u + 1013904223u; return e.rng >> 8; }
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float len(float x, float y) { return sqrtf(x * x + y * y); }

void eye_init(uint32_t seed) {
    memset(&e, 0, sizeof e);
    e.rng = seed | 1;
    e.morph = 1;
}

void eye_look(float x, float y) {
    x = clampf(x, -1, 1), y = clampf(y, -1, 1);
    if (fabsf(x - e.look_x) + fabsf(y - e.look_y) > 0.01f) e.moved_at = e.now;
    e.look_x = x;
    e.look_y = y;
}

void eye_set_state(eye_state_t s) {
    if (s >= EYE_STATE_COUNT || s == e.state) return;
    e.prev = e.state;
    e.state = s;
    e.state_since = e.now;
}

void eye_update(uint32_t now) {
    float dt = e.last ? clampf((now - e.last) / 1000.0f, 0, 0.1f) : 0;
    e.last = e.now = now;
    float t = now / 1000.0f;
    float age = (now - e.state_since) / 1000.0f;
    if (e.state == EYE_DONE && age > DONE_SECONDS) eye_set_state(EYE_IDLE);

    float tx = e.look_x, ty = e.look_y;
    switch (e.state) {
    case EYE_THINKING:  // look up, slowly scanning side to side
        tx = 0.45f * sinf(t * 0.8f);
        ty = -0.55f + 0.05f * sinf(t * 1.3f);
        break;
    case EYE_ERROR:     // shake
        tx = 0.35f * sinf(t * 30) * expf(-age * 2); ty = 0;
        break;
    default:            // cursor still for a while: glance around on its own
        if (now - e.moved_at > 3000) {
            if (now >= e.next_glance) {
                e.glance_x = (rnd() % 1000 / 1000.0f - 0.5f) * 1.2f;
                e.glance_y = (rnd() % 1000 / 1000.0f - 0.5f) * 0.6f;
                e.next_glance = now + 1200 + rnd() % 2000;
            }
            tx = clampf(e.look_x + e.glance_x, -1, 1);
            ty = clampf(e.look_y + e.glance_y, -1, 1);
        }
        break;
    }

    float fast = 1 - expf(-dt * 10);
    e.gaze_x = approach(e.gaze_x, tx, fast);
    e.gaze_y = approach(e.gaze_y, ty, fast);
    e.morph = smoothstep(0, MORPH_SECONDS, age);

    if (now >= e.next_blink) {
        e.blink_start = now;
        e.next_blink = now + 2500 + rnd() % 4000;
    }
    float bt = (now - e.blink_start) / 1000.0f;  // 0.12s closing, 0.12s opening
    e.blink = bt < 0.24f ? 1 - fabsf(bt - 0.12f) / 0.12f : 0;
}

// --- Feature shapes, as signed distances in face coordinates (x right, y down, face radius 1) ---

static float pill(float x, float y, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(x - cx) - hw, qy = fabsf(y - cy) - hh;
    return len(maxf(qx, 0), maxf(qy, 0)) + minf(maxf(qx, qy), 0) - r;
}

static float segment(float x, float y, float ax, float ay, float bx, float by, float w) {
    float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
    float h = clampf((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1);
    return len(px - dx * h, py - dy * h) - w;
}

// Pill eye with a straight top lid cutting across it; the lid's height and slant set the mood.
static float lid_eye(float x, float y, float cx, float lid_y, float slope) {
    float d = pill(x, y, cx, -0.06f, EYE_HW, 0.07f * (1 - e.blink), 0.08f);
    return maxf(d, (lid_y + slope * (x - cx)) - y);
}

static float features(eye_state_t state, float x, float y) {
    switch (state) {
    case EYE_THINKING: return minf(lid_eye(x, y, -0.24f, -0.1f, 0), lid_eye(x, y, 0.24f, -0.02f, 0));      // one lid lower
    case EYE_DONE: {   // squeezed-shut happy chevrons
        float d = 1;
        for (int k = -1; k <= 1; k += 2) {
            float cx = 0.24f * k;
            d = minf(d, minf(segment(x, y, cx - 0.09f, 0.0f, cx, -0.09f, 0.045f), segment(x, y, cx, -0.09f, cx + 0.09f, 0.0f, 0.045f)));
        }
        return d;
    }
    case EYE_ERROR:    return minf(lid_eye(x, y, -0.24f, -0.08f, 0.5f), lid_eye(x, y, 0.24f, -0.08f, -0.5f));  // upset slant
    default: {         // content: no lid, bottoms curved up by the "cheeks" (smiling eyes)
        float d = minf(lid_eye(x, y, -0.24f, -0.5f, 0), lid_eye(x, y, 0.24f, -0.5f, 0));
        float cheek = minf(len(x + 0.24f, y - 0.29f) - 0.25f, len(x - 0.24f, y - 0.29f) - 0.25f);
        return maxf(d, -cheek);
    }
    }
}

// Color of the ball at a screen point (u, v inside the unit circle).
static rgb_t ball(float u, float v, float sy, float cy, float sp, float cp) {
    float r2 = u * u + v * v, z = sqrtf(1 - r2);
    // Rotate the screen point into face coordinates (undo yaw, then pitch).
    float x1 = u * cy - z * sy, z1 = u * sy + z * cy;
    float y2 = v * cp - z1 * sp, z2 = v * sp + z1 * cp;

    // Black glass: faint top-left sheen and a thin rim of light so the sphere reads.
    float sheen = 0.06f * expf(-((u + 0.35f) * (u + 0.35f) + (v + 0.45f) * (v + 0.45f)) * 6);
    float rim = 0.08f * smoothstep(0.9f, 1.0f, sqrtf(r2)) * (0.5f - 0.5f * v);
    float bg = 0.015f + sheen + rim;
    rgb_t col = {bg, bg, bg * 1.1f};

    if (z2 > 0) {
        float fx = x1 / FACE_SCALE, fy = y2 / FACE_SCALE;
        float d = features(e.state, fx, fy) * FACE_SCALE;
        if (e.morph < 1) d = features(e.prev, fx, fy) * FACE_SCALE + (d - features(e.prev, fx, fy) * FACE_SCALE) * e.morph;
        float lit = smoothstep(AA, -AA, d) + 0.18f * expf(-maxf(d, 0) * 18);  // crisp core + soft bloom
        lit *= 0.5f + 0.5f * z2;  // dimmer as features turn away
        const rgb_t white = {0.96f, 0.97f, 1.0f};
        col.r += white.r * lit; col.g += white.g * lit; col.b += white.b * lit;
    }
    return col;
}

// RGB565 has visible steps in dark shades; a 4x4 ordered dither hides the banding.
static uint16_t rgb565(rgb_t c, int x, int y) {
    static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    float t = BAYER[y & 3][x & 3] / 16.0f;
    return (uint16_t)((int)(clampf(c.r, 0, 1) * 31 + t) << 11 |
                      (int)(clampf(c.g, 0, 1) * 63 + t) << 5 |
                      (int)(clampf(c.b, 0, 1) * 31 + t));
}

void eye_render(uint16_t *fb) {
    float yaw = e.gaze_x * 0.6f, pitch = e.gaze_y * 0.5f;
    float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch);
    for (int j = 0; j < EYE_SIZE; j++) {
        float v = (j + 0.5f) / (EYE_SIZE / 2) - 1;
        for (int i = 0; i < EYE_SIZE; i++) {
            float u = (i + 0.5f) / (EYE_SIZE / 2) - 1;
            *fb++ = u * u + v * v < 1 ? rgb565(ball(u, v, sy, cy, sp, cp), i, j) : 0;
        }
    }
}
