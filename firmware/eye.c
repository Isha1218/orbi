// Minimal glowing white eyes on a black glass ball. Lids and eye shape carry the mood;
// expressions morph into each other instead of snapping. Scenes play around the eyes
// (weather fills the whole ball), glance cards briefly replace them.
#include "eye.h"
#include "card.h"
#include "gfx.h"
#include "scene.h"
#include "text.h"
#include <string.h>

#define FACE_SCALE 1.2f         // size of the whole face on the ball
#define AA 0.012f               // edge softness, about one pixel in face units
#define GLOW_REACH 0.15f        // bloom reaches zero at this distance (screen units)
#define GLOW_FLOOR 0.0672f      // expf(-GLOW_REACH * 18)
#define MORPH_RATE 12.0f        // how fast the eye shape follows a new expression
#define FADE_IN 0.25f           // seconds for a scene or card to appear
#define FADE_OUT 0.35f
#define MAX_TEXT 64             // bytes kept from a label
#define LABEL_Y 0.58f           // label center, screen units

// Shape of one eye. Everything a mood changes lives here so moods can be blended.
typedef struct {
    float open;    // height (1 normal)
    float size;    // overall scale (1 normal)
    float lid;     // top lid: 0 none .. 1 closed
    float slope;   // lid slant: positive = inner end lower (cross), negative = worried
    float cheek;   // 0..1 smiling bottom curve
    float squint;  // 0 pill .. 1 happy ^ shape
    float dy;      // vertical offset
} eye_shape_t;

typedef struct {
    eye_shape_t left, right;
    float tilt;    // head tilt, radians
} face_shape_t;

// Eye geometry for the current frame, in face units (x right, y down, face radius 1).
typedef struct {
    float cx, cy;
    float hw, hh, r;
    float lid_y, lid_k;      // lid line: y = lid_y + lid_k * (x - cx)
    float cheek_y;
    float squint;
    float x0, x1, y0, y1;    // bounds including the glow
} eye_geom_t;

enum { CONTENT_NONE, CONTENT_SCENE, CONTENT_ICON, CONTENT_INITIALS, CONTENT_AVATAR };

static const rgb_t WHITE = {0.96f, 0.97f, 1.0f};
static const rgb_t RING_COLOR = {0.3f, 0.7f, 1.0f};
static const rgb_t BLACK = {0, 0, 0};

// open, size, lid, slope, cheek, squint, dy
#define PLAIN {1, 1, 0, 0, 0, 0, 0}
static const face_shape_t FACES[EXPR_COUNT] = {
    [EXPR_IDLE] = {{1, 1, 0, 0, 0.8f, 0, 0}, {1, 1, 0, 0, 0.8f, 0, 0}, 0},
    [EXPR_LISTENING] = {{1.25f, 1.15f, 0, 0, 0, 0, -0.02f}, {1.25f, 1.15f, 0, 0, 0, 0, -0.02f}, 0},
    [EXPR_THINKING] = {{1, 1, 0.35f, 0, 0, 0, 0}, {1, 1, 0.6f, 0, 0, 0, 0}, 0},
    [EXPR_BUSY] = {{0.9f, 0.95f, 0.35f, 0.05f, 0, 0, 0.02f}, {0.9f, 0.95f, 0.35f, 0.05f, 0, 0, 0.02f}, 0},
    [EXPR_PLEASED] = {{1, 1, 0, 0, 1, 1, 0}, {1, 1, 0, 0, 1, 1, 0}, 0},
    [EXPR_WORRIED] = {{0.9f, 0.9f, 0.3f, -0.6f, 0, 0, 0.02f}, {0.9f, 0.9f, 0.3f, -0.6f, 0, 0, 0.02f}, 0},
    [EXPR_EXPECTANT] = {{1.1f, 1.12f, 0, 0, 0.9f, 0, -0.05f}, {1.1f, 1.12f, 0, 0, 0.9f, 0, -0.05f}, 0},
    [EXPR_CONFUSED] = {{1.15f, 1.05f, 0, 0, 0, 0, -0.02f}, {0.8f, 0.95f, 0.35f, 0.3f, 0, 0, 0.02f}, 0.2f},
    [EXPR_SLEEPY] = {{0.9f, 1, 0.55f, -0.15f, 0, 0, 0.04f}, {0.9f, 1, 0.55f, -0.15f, 0, 0, 0.04f}, 0},
};
static const face_shape_t MOODS[MOOD_COUNT] = {
    [MOOD_NEUTRAL] = {PLAIN, PLAIN, 0},
    [MOOD_HAPPY] = {{1.05f, 1.08f, 0, 0, 1, 0, 0}, {1.05f, 1.08f, 0, 0, 1, 0, 0}, 0},
    [MOOD_GRUMPY] = {{0.9f, 1, 0.4f, 0.5f, 0, 0, 0.02f}, {0.9f, 1, 0.4f, 0.5f, 0, 0, 0.02f}, 0},
};

static struct {
    expr_t state;
    float intensity;
    int listening;
    uint32_t state_since, now, last, rng;
    uint32_t next_blink, blink_start, blink_ms;
    uint32_t moved_at, next_glance;
    uint32_t next_side_glance, side_glance_until;
    float side_dir;
    float look_x, look_y;      // target from the cursor
    float glance_x, glance_y;  // where it looks on its own when the cursor is still
    float gaze_x, gaze_y;      // smoothed: where the face points
    float blink;               // 0 open .. 1 closed
    face_shape_t shown;        // smoothed shape being drawn

    // content: a scene or a card
    int content;
    int scene, mood, icon;
    uint32_t content_start, content_end;
    float content_alpha;
    text_t label;
    float label_alpha;
    float label_rise;          // pixels the label still has to rise

    // per-frame values used by the renderer
    float ring;                // listening ring strength
    float sin_yaw, cos_yaw, sin_pitch, cos_pitch;
    float sin_tilt, cos_tilt;
    eye_geom_t eye[2];
    float eu0, eu1, ev0, ev1;  // screen bounds of the eyes
    float eyes_alpha;
    float time;
} e;

static rgb_t row[EYE_SIZE];

static uint32_t rnd(void) {
    e.rng = e.rng * 1664525u + 1013904223u;
    return e.rng >> 8;
}

static float rnd01(void) {
    return (rnd() % 1000) / 1000.0f;
}

static eye_shape_t lerp_eye(eye_shape_t a, eye_shape_t b, float k) {
    eye_shape_t out;
    out.open = lerp(a.open, b.open, k);
    out.size = lerp(a.size, b.size, k);
    out.lid = lerp(a.lid, b.lid, k);
    out.slope = lerp(a.slope, b.slope, k);
    out.cheek = lerp(a.cheek, b.cheek, k);
    out.squint = lerp(a.squint, b.squint, k);
    out.dy = lerp(a.dy, b.dy, k);
    return out;
}

static face_shape_t lerp_face(face_shape_t a, face_shape_t b, float k) {
    face_shape_t out;
    out.left = lerp_eye(a.left, b.left, k);
    out.right = lerp_eye(a.right, b.right, k);
    out.tilt = lerp(a.tilt, b.tilt, k);
    return out;
}

// --- Labels ---

// Label width allowed at height y (pixels): the circle's chord, minus a margin.
static float chord_width(float y_px, float half_height) {
    float v = (fabsf(y_px - HALF) + half_height) / HALF;
    float w = 2 * sqrtf(maxf(1 - v * v, 0)) * HALF;
    return w - 40;
}

static void set_label(const char *label) {
    char copy[MAX_TEXT];
    copy[0] = 0;
    if (label != NULL) {
        strncpy(copy, label, MAX_TEXT - 1);
        copy[MAX_TEXT - 1] = 0;
    }
    float cy = HALF + LABEL_Y * HALF;
    float half = text_line_height(FONT_SMALL) / 2.0f;
    text_layout(&e.label, FONT_SMALL, copy, HALF, cy, chord_width(cy, half));
}

// --- Public API ---

void eye_init(uint32_t seed) {
    memset(&e, 0, sizeof e);
    e.rng = seed | 1;
    e.intensity = 1;
    e.shown = FACES[EXPR_IDLE];
    e.blink_ms = 240;
}

void eye_gaze(float x, float y) {
    x = clampf(x, -1, 1);
    y = clampf(y, -1, 1);
    if (fabsf(x - e.look_x) + fabsf(y - e.look_y) > 0.01f) {
        e.moved_at = e.now;
    }
    e.look_x = x;
    e.look_y = y;
}

void eye_set_expression(expr_t s, float intensity) {
    if (s >= EXPR_COUNT) {
        return;
    }
    e.intensity = clampf(intensity, 0, 1);
    if (s == e.state) {
        return;
    }
    e.state = s;
    e.state_since = e.now;
    e.next_side_glance = e.now + 2000;
}

void eye_blink(void) {
    e.next_blink = e.now;
}

void eye_set_listening(int on) {
    e.listening = on;
}

static int showing_card(void) {
    if (e.content == CONTENT_NONE || e.content == CONTENT_SCENE) {
        return 0;
    }
    return 1;
}

static int showing_weather(void) {
    return e.content == CONTENT_SCENE && scene_is_weather(e.scene);
}

static void start_content(int kind, const char *label, uint32_t duration_ms) {
    e.content = kind;
    e.content_start = e.now;
    e.content_end = e.now + duration_ms;
    set_label(label);
}

void eye_scene(scene_t scene, mood_t mood, const char *label, uint32_t duration_ms) {
    if (scene >= SCENE_COUNT || mood >= MOOD_COUNT) {
        return;
    }
    e.scene = scene;
    e.mood = mood;
    start_content(CONTENT_SCENE, label, duration_ms);
}

void eye_card_icon(icon_t icon, const char *label, uint32_t duration_ms) {
    if (icon >= ICON_COUNT) {
        return;
    }
    e.icon = icon;
    start_content(CONTENT_ICON, label, duration_ms);
}

void eye_card_initials(const char *initials, const char *label, uint32_t duration_ms) {
    card_set_initials(initials);
    start_content(CONTENT_INITIALS, label, duration_ms);
}

void eye_card_avatar(const char *label, uint32_t duration_ms) {
    start_content(CONTENT_AVATAR, label, duration_ms);
}

void eye_set_avatar(const uint16_t *px) {
    card_set_avatar(px);
}

// --- Per-frame update ---

// Where the eyes look when nothing else is going on: the cursor, or glances when it is still.
static void cursor_target(float *tx, float *ty) {
    *tx = e.look_x;
    *ty = e.look_y;
    if (e.now - e.moved_at > 3000) {
        if (e.now >= e.next_glance) {
            e.glance_x = (rnd01() - 0.5f) * 1.2f;
            e.glance_y = (rnd01() - 0.5f) * 0.6f;
            e.next_glance = e.now + 1200 + rnd() % 2000;
        }
        *tx = clampf(e.look_x + e.glance_x, -1, 1);
        *ty = clampf(e.look_y + e.glance_y, -1, 1);
    }
}

// Sleepy yawn: every 10 s the lids sink shut and the head tips back, 0..1.
static float yawn_amount(float age) {
    float phase = fmodf(age, 10.0f);
    if (age < 3 || phase < 6) {
        return 0;
    }
    float p = phase - 6;
    if (p < 0.8f) {
        return smoothstep(0, 0.8f, p);
    }
    if (p < 1.4f) {
        return 1;
    }
    return 1 - smoothstep(1.4f, 2.5f, p);
}

static void expression_target(float t, float age, float *tx, float *ty) {
    float cx;
    float cy;
    cursor_target(&cx, &cy);
    float bx = cx;
    float by = cy;
    switch (e.state) {
    case EXPR_LISTENING:  // straight at you
        bx = 0;
        by = 0;
        break;
    case EXPR_THINKING:  // look up, slowly scanning side to side
        bx = 0.45f * sinf(t * 0.8f);
        by = -0.55f + 0.05f * sinf(t * 1.3f);
        break;
    case EXPR_BUSY:  // eyes on the work, with a quick side glance now and then
        if (e.now >= e.next_side_glance) {
            e.side_dir = (rnd() % 2) ? 1.0f : -1.0f;
            e.side_glance_until = e.now + 700;
            e.next_side_glance = e.now + 3000 + rnd() % 3000;
        }
        bx = 0.1f * sinf(t * 0.5f);
        by = 0.3f;
        if (e.now < e.side_glance_until) {
            bx = 0.75f * e.side_dir;
            by = 0.05f;
        }
        break;
    case EXPR_WORRIED:  // a shake when it starts, then a low gaze
        bx = 0.35f * sinf(t * 30) * expf(-age * 2);
        by = 0.15f;
        break;
    case EXPR_EXPECTANT:  // looking up at you, bobbing a little
        bx = 0;
        by = -0.15f + 0.06f * sinf(t * 2.5f);
        break;
    case EXPR_CONFUSED:
        bx = 0.25f;
        by = -0.1f;
        break;
    case EXPR_SLEEPY:
        bx = 0.2f * sinf(t * 0.3f);
        by = 0.3f - 0.4f * yawn_amount(age);
        break;
    default:
        break;
    }
    *tx = lerp(cx, bx, e.intensity);
    *ty = lerp(cy, by, e.intensity);
}

static void scene_target(float t, float *tx, float *ty) {
    switch (e.scene) {
    case SCENE_SUN:  // up at the sun
        *tx = 0.25f + 0.05f * sinf(t * 0.6f);
        *ty = -0.15f;
        break;
    case SCENE_STORM:  // nervously up at the cloud
        *tx = 0.2f * sinf(t * 1.1f);
        *ty = -0.3f;
        break;
    case SCENE_CONFETTI:
        *tx = 0.5f * sinf(t * 1.7f);
        *ty = -0.3f + 0.2f * sinf(t * 1.1f);
        break;
    case SCENE_SPARKLES:
        *tx = 0.5f * sinf(t * 1.3f);
        *ty = -0.2f + 0.25f * sinf(t * 0.9f);
        break;
    case SCENE_ZZZ:
        *tx = 0.15f * sinf(t * 0.4f);
        *ty = 0.25f;
        break;
    default:  // rain, snow: looking up at what falls
        *tx = 0.3f * sinf(t * 0.7f);
        *ty = -0.6f;
        break;
    }
}

static void build_eye(eye_geom_t *g, const eye_shape_t *s, float side, float blink, float extra_lid) {
    g->cx = 0.24f * side;
    g->cy = -0.06f + s->dy;
    g->r = 0.08f * s->size;
    g->hw = (0.05f + 0.035f * blink) * s->size;  // widens as it blinks (squash and stretch)
    g->hh = 0.07f * s->size * maxf(s->open, 0) * (1 - blink);
    float lid = s->lid + (1 - s->lid) * extra_lid;
    float top = g->cy - g->hh - g->r;
    g->lid_y = top + lid * 2 * (g->hh + g->r);
    g->lid_k = -side * s->slope;
    g->cheek_y = g->cy + 0.35f + (1 - s->cheek) * 0.15f;
    g->squint = s->squint;
    float margin = GLOW_REACH / FACE_SCALE;
    g->x0 = g->cx - g->hw - g->r - margin;
    g->x1 = g->cx + g->hw + g->r + margin;
    g->y0 = g->cy - g->hh - g->r - margin;
    g->y1 = g->cy + g->hh + g->r + margin;
}

// Screen bounds of the eyes: project points around their box onto the ball.
static void eye_screen_bounds(void) {
    float x0 = minf(e.eye[0].x0, e.eye[1].x0);
    float x1 = maxf(e.eye[0].x1, e.eye[1].x1);
    float y0 = minf(e.eye[0].y0, e.eye[1].y0);
    float y1 = maxf(e.eye[0].y1, e.eye[1].y1);
    e.eu0 = 2;
    e.eu1 = -2;
    e.ev0 = 2;
    e.ev1 = -2;
    for (int k = 0; k < 20; k++) {
        float f = (k % 5) / 4.0f;
        float lx;
        float ly;
        if (k < 5) {
            lx = lerp(x0, x1, f);
            ly = y0;
        } else if (k < 10) {
            lx = lerp(x0, x1, f);
            ly = y1;
        } else if (k < 15) {
            lx = x0;
            ly = lerp(y0, y1, f);
        } else {
            lx = x1;
            ly = lerp(y0, y1, f);
        }
        // undo the head tilt, then go from face to screen
        float fx = (lx * e.cos_tilt - ly * e.sin_tilt) * FACE_SCALE;
        float fy = (lx * e.sin_tilt + ly * e.cos_tilt) * FACE_SCALE;
        float fz = sqrtf(maxf(1 - fx * fx - fy * fy, 0));
        float v = fy * e.cos_pitch + fz * e.sin_pitch;
        float z1 = -fy * e.sin_pitch + fz * e.cos_pitch;
        float u = fx * e.cos_yaw + z1 * e.sin_yaw;
        e.eu0 = minf(e.eu0, u);
        e.eu1 = maxf(e.eu1, u);
        e.ev0 = minf(e.ev0, v);
        e.ev1 = maxf(e.ev1, v);
    }
    e.eu0 -= 0.03f;
    e.eu1 += 0.03f;
    e.ev0 -= 0.03f;
    e.ev1 += 0.03f;
}

static float fade(uint32_t start, uint32_t end) {
    float age = (e.now - start) / 1000.0f;
    float left = ((int32_t)(end - e.now)) / 1000.0f;
    return minf(smoothstep(0, FADE_IN, age), smoothstep(0, FADE_OUT, left));
}

static void update_content(void) {
    float age = (e.now - e.content_start) / 1000.0f;
    float left = ((int32_t)(e.content_end - e.now)) / 1000.0f;
    e.label_alpha = smoothstep(0.15f, 0.45f, age) * smoothstep(0, 0.3f, left);
    e.label_rise = 10 * (1 - ease_out((age - 0.15f) / 0.5f));
    if (e.content == CONTENT_SCENE) {
        scene_update(e.scene, age, left);
    } else if (e.content == CONTENT_ICON) {
        card_update(CARD_ICON, e.icon, age, left);
    } else if (e.content == CONTENT_INITIALS) {
        card_update(CARD_INITIALS, 0, age, left);
    } else if (e.content == CONTENT_AVATAR) {
        card_update(CARD_AVATAR, 0, age, left);
    }
}

void eye_update(uint32_t now) {
    float dt = 0;
    if (e.last) {
        dt = clampf((now - e.last) / 1000.0f, 0, 0.1f);
    }
    e.last = now;
    e.now = now;
    float t = now / 1000.0f;
    float age = (now - e.state_since) / 1000.0f;
    e.time = t;

    if (e.content != CONTENT_NONE && (int32_t)(now - e.content_end) >= 0) {
        e.content = CONTENT_NONE;
    }
    e.content_alpha = 0;
    if (e.content != CONTENT_NONE) {
        e.content_alpha = fade(e.content_start, e.content_end);
    }
    int scene_on = e.content == CONTENT_SCENE && !showing_weather();  // a scene the eyes react to
    float scene_t = (now - e.content_start) / 1000.0f;

    // Shape: the expression scaled by intensity, or the scene's mood.
    face_shape_t target = lerp_face(FACES[EXPR_IDLE], FACES[e.state], e.intensity);
    if (scene_on) {
        target = MOODS[e.mood];
    }
    if (scene_on && e.scene == SCENE_ZZZ) {  // fast asleep, breathing slowly
        float breath = 0.02f * sinf(scene_t * 2 * PI / 3.2f);
        target.left.lid = maxf(target.left.lid, 0.88f);
        target.right.lid = maxf(target.right.lid, 0.88f);
        target.left.dy += breath;
        target.right.dy += breath;
    }
    float k = 1 - expf(-dt * MORPH_RATE);
    e.shown = lerp_face(e.shown, target, k);

    float tx;
    float ty;
    if (scene_on) {
        scene_target(t, &tx, &ty);
    } else {
        expression_target(t, age, &tx, &ty);
    }
    float fast = 1 - expf(-dt * 10);
    e.gaze_x = lerp(e.gaze_x, tx, fast);
    e.gaze_y = lerp(e.gaze_y, ty, fast);

    if (now >= e.next_blink) {
        e.blink_start = now;
        e.blink_ms = 240;
        if (e.state == EXPR_SLEEPY) {
            e.blink_ms = 600;
        }
        e.next_blink = now + 2500 + rnd() % 4000;
    }
    float half = e.blink_ms / 2000.0f;
    float bt = (now - e.blink_start) / 1000.0f;
    e.blink = 0;
    if (bt < 2 * half) {
        e.blink = 1 - fabsf(bt - half) / half;
    }

    // Extra lid drop while yawning.
    float extra_lid = 0;
    if (!scene_on && e.state == EXPR_SLEEPY) {
        extra_lid = 0.9f * yawn_amount(age) * e.intensity;
    }

    float listen_k = 1 - expf(-dt * 6);
    e.ring = lerp(e.ring, e.listening ? 1.0f : 0.0f, listen_k);

    // Values the renderer needs for this frame.
    float yaw = e.gaze_x * 0.6f;
    float pitch = e.gaze_y * 0.5f;
    e.sin_yaw = sinf(yaw);
    e.cos_yaw = cosf(yaw);
    e.sin_pitch = sinf(pitch);
    e.cos_pitch = cosf(pitch);
    e.sin_tilt = sinf(e.shown.tilt);
    e.cos_tilt = cosf(e.shown.tilt);
    build_eye(&e.eye[0], &e.shown.left, -1, e.blink, extra_lid);
    build_eye(&e.eye[1], &e.shown.right, 1, e.blink, extra_lid);
    eye_screen_bounds();

    // Cards and weather replace the eyes: they leave quickly and come back as the content goes.
    e.eyes_alpha = 1;
    if (showing_card() || showing_weather()) {
        float shown = (now - e.content_start) / 1000.0f;
        float left = ((int32_t)(e.content_end - now)) / 1000.0f;
        e.eyes_alpha = 1 - minf(smoothstep(0, 0.15f, shown), smoothstep(0, 0.25f, left));
    }
    if (e.content != CONTENT_NONE) {
        update_content();
    }
}

// --- Shapes, as signed distances ---

static float chevron(float x, float y, float cx, float cy, float w) {
    float a = segment(x, y, cx - 0.09f, cy + 0.06f, cx, cy - 0.03f, w);
    float b = segment(x, y, cx, cy - 0.03f, cx + 0.09f, cy + 0.06f, w);
    return minf(a, b);
}

// Pill eye with a straight top lid cutting across it and a cheek pushing up from below.
static float eye_sdf(const eye_geom_t *g, float x, float y) {
    float d = 1;
    if (g->squint < 0.999f) {
        d = pill(x, y, g->cx, g->cy, g->hw, g->hh, g->r);
        float lid = g->lid_y + g->lid_k * (x - g->cx) - y;
        d = maxf(d, lid);
        float cheek = 0.25f - len(x - g->cx, y - g->cheek_y);
        d = maxf(d, cheek);
    }
    if (g->squint > 0.001f) {
        float c = chevron(x, y, g->cx, g->cy, 0.045f);
        if (g->squint >= 0.999f) {
            d = c;
        } else {
            d = lerp(d, c, g->squint);
        }
    }
    return d;
}


// --- Rendering ---

// Crisp core plus soft bloom, for a signed distance in screen units.
static float glow(float d) {
    float lit = smoothstep(AA, -AA, d);
    if (d < GLOW_REACH) {
        lit += 0.18f * (expf(-maxf(d, 0) * 18) - GLOW_FLOOR);
    }
    return lit;
}

static float eyes_lit(float u, float v, float z) {
    // Rotate the screen point into face coordinates (undo yaw, then pitch).
    float x1 = u * e.cos_yaw - z * e.sin_yaw;
    float z1 = u * e.sin_yaw + z * e.cos_yaw;
    float y2 = v * e.cos_pitch - z1 * e.sin_pitch;
    float z2 = v * e.sin_pitch + z1 * e.cos_pitch;
    if (z2 <= 0) {
        return 0;
    }
    float fx = x1 / FACE_SCALE;
    float fy = y2 / FACE_SCALE;
    // undo the head tilt
    float lx = fx * e.cos_tilt + fy * e.sin_tilt;
    float ly = -fx * e.sin_tilt + fy * e.cos_tilt;

    float d = 1;
    for (int k = 0; k < 2; k++) {
        const eye_geom_t *g = &e.eye[k];
        if (lx < g->x0 || lx > g->x1 || ly < g->y0 || ly > g->y1) {
            continue;
        }
        d = minf(d, eye_sdf(g, lx, ly));
    }
    if (d * FACE_SCALE >= GLOW_REACH) {
        return 0;
    }
    float lit = glow(d * FACE_SCALE);
    return lit * (0.5f + 0.5f * z2);  // dimmer as features turn away
}

// RGB565 has visible steps in dark shades; a 4x4 ordered dither hides the banding.
static uint16_t rgb565(rgb_t c, int x, int y) {
    static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    float t = BAYER[y & 3][x & 3] / 16.0f;
    int r = (int)(clampf(c.r, 0, 1) * 31 + t);
    int g = (int)(clampf(c.g, 0, 1) * 63 + t);
    int b = (int)(clampf(c.b, 0, 1) * 31 + t);
    return (uint16_t)(r << 11 | g << 5 | b);
}

// Black glass: faint top-left sheen, a thin rim of light so the sphere reads, and the listening ring.
static void glass_row(float v, int skip) {
    for (int i = 0; i < EYE_SIZE; i++) {
        float u = (i + 0.5f) / HALF - 1;
        float r2 = u * u + v * v;
        if (r2 >= 1 || skip) {
            row[i] = BLACK;
            continue;
        }
        float bg = 0.015f;
        float su = u + 0.35f;
        float sv = v + 0.45f;
        float s2 = su * su + sv * sv;
        if (s2 < 0.6f) {
            bg += 0.06f * expf(-s2 * 6);
        }
        rgb_t col = {bg, bg, bg * 1.1f};
        if (r2 > 0.77f) {
            float r = sqrtf(r2);
            float rim = 0.08f * smoothstep(0.9f, 1.0f, r) * (0.5f - 0.5f * v);
            add_color(&col, (rgb_t){1, 1, 1.1f}, rim);
            if (e.ring > 0.01f) {
                float d = (r - 0.93f) / 0.022f;
                float pulse = 0.75f + 0.25f * sinf(e.time * 3);
                add_color(&col, RING_COLOR, expf(-d * d) * pulse * e.ring);
            }
        }
        row[i] = col;
    }
}

// The label sits on a soft dark backing so it stays readable over busy scenes.
static void label_row(float py) {
    const text_t *t = &e.label;
    float rise = e.label_rise;
    float cx = (t->x0 + t->x1) / 2;
    float cy = (t->y0 + t->y1) / 2 + rise;
    float hh = (t->y1 - t->y0) / 2 + 7;
    float hw = (t->x1 - t->x0) / 2 + 14;
    if (py < cy - hh - 8 || py > cy + hh + 8) {
        return;
    }
    float k = e.label_alpha;
    int i0 = (int)maxf(cx - hw - 8, 0);
    int i1 = (int)minf(cx + hw + 8, EYE_SIZE);
    for (int i = i0; i < i1; i++) {
        float px = i + 0.5f;
        float d = pill(px, py, cx, cy, hw - hh, 0, hh);
        float backing = clampf(0.5f - d / 14, 0, 1);
        over(&row[i], BLACK, backing * 0.45f * k);
        float shadow = text_alpha(t, px, py - rise - 1.5f);
        over(&row[i], BLACK, shadow * 0.6f * k);
        float a = text_alpha(t, px, py - rise);
        over(&row[i], WHITE, a * k);
    }
}

static void render_row(uint16_t *out, int j) {
    float v = (j + 0.5f) / HALF - 1;
    float py = j + 0.5f;
    int scene = e.content == CONTENT_SCENE;
    int card = showing_card();

    glass_row(v, scene && scene_covers());
    if (scene) {
        scene_draw_back(row, j);
    }
    if (card) {
        card_draw(row, j);
    }
    if (e.eyes_alpha > 0 && v >= e.ev0 && v <= e.ev1) {
        for (int i = 0; i < EYE_SIZE; i++) {
            float u = (i + 0.5f) / HALF - 1;
            float r2 = u * u + v * v;
            if (r2 >= 1 || u < e.eu0 || u > e.eu1) {
                continue;
            }
            float lit = eyes_lit(u, v, sqrtf(1 - r2));
            add_color(&row[i], WHITE, lit * e.eyes_alpha);
        }
    }
    if (scene) {
        scene_draw_front(row, j);
    }
    if (e.content != CONTENT_NONE && e.label.count > 0) {
        label_row(py);
    }
    for (int i = 0; i < EYE_SIZE; i++) {
        float u = (i + 0.5f) / HALF - 1;
        if (u * u + v * v >= 1) {
            out[i] = 0;
        } else {
            out[i] = rgb565(row[i], i, j);
        }
    }
}

void eye_render_rows(uint16_t *buf, int y0, int y1) {
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > EYE_SIZE) {
        y1 = EYE_SIZE;
    }
    for (int j = y0; j < y1; j++) {
        render_row(buf, j);
        buf += EYE_SIZE;
    }
}

void eye_render(uint16_t *fb) {
    eye_render_rows(fb, 0, EYE_SIZE);
}
