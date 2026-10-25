// Minimal glowing white eyes on a black glass ball. Lids and eye shape carry the mood;
// expressions morph into each other instead of snapping. Scenes play around the eyes,
// glance cards briefly replace them.
#include "eye.h"
#include "font.h"
#include <math.h>
#include <string.h>

#define HALF (EYE_SIZE / 2)
#define PX (1.0f / HALF)        // one pixel in screen units (screen radius 1)
#define FACE_SCALE 1.2f         // size of the whole face on the ball
#define AA 0.012f               // edge softness, about one pixel in face units
#define GLOW_REACH 0.15f        // bloom reaches zero at this distance (screen units)
#define GLOW_FLOOR 0.0672f      // expf(-GLOW_REACH * 18)
#define MORPH_RATE 12.0f        // how fast the eye shape follows a new expression
#define FADE_IN 0.25f           // seconds for a scene or card to appear
#define FADE_OUT 0.35f
#define MAX_PARTICLES 40
#define MAX_GLYPHS 32
#define MAX_TEXT 64             // bytes kept from a label
#define LABEL_Y 0.58f           // label center, screen units
#define CARD_Y (-0.14f)         // card content center
#define CARD_R 0.46f            // card circle radius

typedef struct {
    float r, g, b;
} rgb_t;

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

enum { P_DOT, P_STREAK, P_BOLT, P_RECT, P_STAR, P_ZED, P_SUN, P_CLOUD };

typedef struct {
    int kind;
    float x, y;
    float ax, ay;            // streak end offset, or rotation (cos, sin)
    float a, b;              // sizes, meaning depends on kind
    rgb_t color;             // already scaled by the fade
    float x0, x1, y0, y1;    // bounds in screen units
} particle_t;

typedef struct {
    const font_glyph_t *glyphs;
    const uint8_t *alpha;
    int count;
} font_t;

typedef struct {
    const font_t *font;
    int count;
    const font_glyph_t *glyph[MAX_GLYPHS];
    float x[MAX_GLYPHS];     // left edge of each bitmap, pixels
    float top;               // line top, pixels
    float scale;
    float x0, x1, y0, y1;    // ink bounds, pixels
} text_t;

enum { CONTENT_NONE, CONTENT_SCENE, CONTENT_ICON, CONTENT_INITIALS, CONTENT_AVATAR };

static const font_t SMALL_FONT = {FONT_SMALL_GLYPHS, FONT_SMALL_ALPHA, FONT_SMALL_COUNT};
static const font_t BIG_FONT = {FONT_BIG_GLYPHS, FONT_BIG_ALPHA, FONT_BIG_COUNT};

static const rgb_t WHITE = {0.96f, 0.97f, 1.0f};
static const rgb_t RING_COLOR = {0.3f, 0.7f, 1.0f};

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

static const rgb_t ICON_COLORS[ICON_COUNT] = {
    [ICON_MAIL] = {0.8f, 0.9f, 1.0f},
    [ICON_CALENDAR] = {0.95f, 0.95f, 1.0f},
    [ICON_CHECK] = {0.5f, 1.0f, 0.6f},
    [ICON_CROSS] = {1.0f, 0.45f, 0.45f},
    [ICON_WARNING] = {1.0f, 0.8f, 0.3f},
    [ICON_CLOCK] = {0.95f, 0.95f, 1.0f},
    [ICON_BELL] = {1.0f, 0.9f, 0.5f},
    [ICON_CHAT] = {0.6f, 0.8f, 1.0f},
    [ICON_CODE] = {0.6f, 1.0f, 0.85f},
    [ICON_MUSIC] = {0.85f, 0.65f, 1.0f},
    [ICON_STAR] = {1.0f, 0.85f, 0.4f},
    [ICON_HEART] = {1.0f, 0.45f, 0.6f},
};

static const rgb_t CONFETTI_COLORS[5] = {
    {1.0f, 0.4f, 0.6f},
    {1.0f, 0.85f, 0.3f},
    {0.3f, 0.85f, 1.0f},
    {0.5f, 1.0f, 0.5f},
    {1.0f, 0.6f, 0.25f},
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
    text_t initials;

    // per-frame values used by the renderer
    float ring;                // listening ring strength
    float sin_yaw, cos_yaw, sin_pitch, cos_pitch;
    float sin_tilt, cos_tilt;
    eye_geom_t eye[2];
    float eu0, eu1, ev0, ev1;  // screen bounds of the eyes
    float eyes_alpha;
    float flash;               // storm lightning
    float time;
    particle_t parts[MAX_PARTICLES];
    int part_count;
} e;

static uint16_t avatar[AVATAR_SIZE * AVATAR_SIZE];

// --- Small helpers ---

static float clampf(float x, float lo, float hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

static float smoothstep(float a, float b, float x) {
    x = clampf((x - a) / (b - a), 0, 1);
    return x * x * (3 - 2 * x);
}

static float minf(float a, float b) {
    return a < b ? a : b;
}

static float maxf(float a, float b) {
    return a > b ? a : b;
}

static float lerp(float a, float b, float k) {
    return a + (b - a) * k;
}

static float len(float x, float y) {
    return sqrtf(x * x + y * y);
}

static uint32_t rnd(void) {
    e.rng = e.rng * 1664525u + 1013904223u;
    return e.rng >> 8;
}

static float rnd01(void) {
    return (rnd() % 1000) / 1000.0f;
}

// Stable pseudo-random 0..1 for particle n, so particles need no stored state.
static float hash01(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return (n & 0xffffu) / 65535.0f;
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

// --- Text ---

// Decodes one UTF-8 character and advances *s. Bad bytes become U+FFFD.
static uint32_t next_codepoint(const char **s) {
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = p[0];
    int extra = 0;
    if (c < 0x80) {
        extra = 0;
    } else if ((c & 0xe0) == 0xc0) {
        c = c & 0x1f;
        extra = 1;
    } else if ((c & 0xf0) == 0xe0) {
        c = c & 0x0f;
        extra = 2;
    } else if ((c & 0xf8) == 0xf0) {
        c = c & 0x07;
        extra = 3;
    } else {
        *s += 1;
        return 0xfffd;
    }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xc0) != 0x80) {
            *s += i;
            return 0xfffd;
        }
        c = (c << 6) | (p[i] & 0x3f);
    }
    *s += 1 + extra;
    return c;
}

static const font_glyph_t *find_glyph(const font_t *f, uint32_t code) {
    for (int i = 0; i < f->count; i++) {
        if (f->glyphs[i].code == code) {
            return &f->glyphs[i];
        }
    }
    return NULL;
}

// Lays out one line centered on (cx, cy) in pixels, shrunk if it is wider than max_w.
static void text_layout(text_t *t, const font_t *f, const char *s, float cx, float cy, float max_w) {
    memset(t, 0, sizeof *t);
    t->font = f;
    if (s == NULL) {
        return;
    }
    const font_glyph_t *fallback = find_glyph(f, '?');
    const font_glyph_t *cap = find_glyph(f, 'H');  // lines are centered on the capital height
    float pen = 0;
    float ink_top = 1000;
    float ink_bottom = -1000;
    while (*s && t->count < MAX_GLYPHS) {
        uint32_t code = next_codepoint(&s);
        const font_glyph_t *g = find_glyph(f, code);
        if (g == NULL) {
            g = fallback;
        }
        if (g == NULL) {
            continue;
        }
        t->glyph[t->count] = g;
        t->x[t->count] = pen + g->x_off;
        t->count++;
        pen += g->advance;
        if (g->h > 0) {
            ink_top = minf(ink_top, g->y_off);
            ink_bottom = maxf(ink_bottom, g->y_off + g->h);
        }
    }
    if (t->count == 0 || ink_bottom < ink_top) {
        t->count = 0;
        return;
    }

    t->scale = 1;
    if (pen > max_w) {
        t->scale = max_w / pen;
    }
    float left = cx - pen * t->scale / 2;
    float middle = (ink_top + ink_bottom) / 2;
    if (cap != NULL) {
        middle = cap->y_off + cap->h / 2.0f;
    }
    t->top = cy - middle * t->scale;
    if (t->scale == 1) {
        left = floorf(left + 0.5f);
        t->top = floorf(t->top + 0.5f);
    }

    t->x0 = 1e9f;
    t->x1 = -1e9f;
    for (int i = 0; i < t->count; i++) {
        t->x[i] = left + t->x[i] * t->scale;
        t->x0 = minf(t->x0, t->x[i]);
        t->x1 = maxf(t->x1, t->x[i] + t->glyph[i]->w * t->scale);
    }
    t->x0 -= 1;
    t->x1 += 1;
    t->y0 = t->top + ink_top * t->scale - 1;
    t->y1 = t->top + ink_bottom * t->scale + 1;
}

static float glyph_pixel(const font_t *f, const font_glyph_t *g, int x, int y) {
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) {
        return 0;
    }
    return f->alpha[g->offset + y * g->w + x] / 255.0f;
}

// Coverage of the text at pixel center (px, py), 0..1.
static float text_alpha(const text_t *t, float px, float py) {
    if (px < t->x0 || px > t->x1 || py < t->y0 || py > t->y1) {
        return 0;
    }
    float total = 0;
    for (int i = 0; i < t->count; i++) {
        const font_glyph_t *g = t->glyph[i];
        float gx = (px - t->x[i]) / t->scale - 0.5f;
        if (gx < -1 || gx > g->w) {
            continue;
        }
        float gy = (py - t->top) / t->scale - g->y_off - 0.5f;
        if (gy < -1 || gy > g->h) {
            continue;
        }
        int ix = (int)floorf(gx);
        int iy = (int)floorf(gy);
        float fx = gx - ix;
        float fy = gy - iy;
        float top = lerp(glyph_pixel(t->font, g, ix, iy), glyph_pixel(t->font, g, ix + 1, iy), fx);
        float bottom = lerp(glyph_pixel(t->font, g, ix, iy + 1), glyph_pixel(t->font, g, ix + 1, iy + 1), fx);
        total += lerp(top, bottom, fy);
    }
    return minf(total, 1);
}

// Label width allowed at height y (pixels): the circle's chord, minus a margin.
static float chord_width(float y_px, float half_height) {
    float v = (fabsf(y_px - HALF) + half_height) / HALF;
    float w = 2 * sqrtf(maxf(1 - v * v, 0)) * HALF;
    return w - 24;
}

static void set_label(const char *label) {
    char copy[MAX_TEXT];
    copy[0] = 0;
    if (label != NULL) {
        strncpy(copy, label, MAX_TEXT - 1);
        copy[MAX_TEXT - 1] = 0;
    }
    float cy = HALF + LABEL_Y * HALF;
    text_layout(&e.label, &SMALL_FONT, copy, HALF, cy, chord_width(cy, FONT_SMALL_HEIGHT / 2));
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
    char upper[4];
    int n = 0;
    while (initials && initials[n] && n < 3) {
        char c = initials[n];
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
        upper[n] = c;
        n++;
    }
    upper[n] = 0;
    float cy = HALF + CARD_Y * HALF;
    text_layout(&e.initials, &BIG_FONT, upper, HALF, cy, CARD_R * HALF * 1.4f);
    start_content(CONTENT_INITIALS, label, duration_ms);
}

void eye_card_avatar(const char *label, uint32_t duration_ms) {
    start_content(CONTENT_AVATAR, label, duration_ms);
}

void eye_set_avatar(const uint16_t *px) {
    memcpy(avatar, px, sizeof avatar);
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

static particle_t *add_particle(int kind, float x, float y, float reach) {
    if (e.part_count >= MAX_PARTICLES) {
        return NULL;
    }
    particle_t *p = &e.parts[e.part_count];
    e.part_count++;
    memset(p, 0, sizeof *p);
    p->kind = kind;
    p->x = x;
    p->y = y;
    p->x0 = x - reach;
    p->x1 = x + reach;
    p->y0 = y - reach;
    p->y1 = y + reach;
    return p;
}

static rgb_t scaled(rgb_t c, float k) {
    rgb_t out = {c.r * k, c.g * k, c.b * k};
    return out;
}

static void add_streak(float x, float y, float dx, float dy, float w, rgb_t color) {
    particle_t *p = add_particle(P_STREAK, x, y, 0);
    if (p == NULL) {
        return;
    }
    p->ax = dx;
    p->ay = dy;
    p->b = w;
    p->color = color;
    p->x0 = minf(x, x + dx) - w - 2 * PX;
    p->x1 = maxf(x, x + dx) + w + 2 * PX;
    p->y0 = minf(y, y + dy) - w - 2 * PX;
    p->y1 = maxf(y, y + dy) + w + 2 * PX;
}

static void add_rain(float t, int count, float left, float right, float top, float alpha) {
    rgb_t color = scaled((rgb_t){0.45f, 0.65f, 1.0f}, 0.7f * alpha);
    float span = 1.0f - top + 0.15f;
    for (int i = 0; i < count; i++) {
        float x = lerp(left, right, hash01(i));
        float speed = 1.6f + 0.6f * hash01(i + 100);
        float y = top + fmodf(hash01(i + 200) * span + t * speed, span) - 0.15f;
        add_streak(x, y, -0.025f, 0.12f, 0.007f, color);
    }
}

static void add_bolt(float t, float alpha) {
    int strike = (int)(t / 1.8f);
    float since = t - strike * 1.8f;
    int on = since < 0.08f || (since > 0.14f && since < 0.26f);
    if (!on || t < 0.5f) {
        e.flash = 0;
        return;
    }
    e.flash = 0.05f * alpha;
    rgb_t color = scaled((rgb_t){1.0f, 0.95f, 0.65f}, alpha);
    float x = (hash01(strike + 300) - 0.5f) * 0.4f;
    float y = -0.5f;
    for (int k = 0; k < 4; k++) {
        float nx = x + (hash01(strike * 7 + k) - 0.5f) * 0.18f;
        float ny = y + 0.13f;
        particle_t *p = add_particle(P_BOLT, x, y, 0);
        if (p == NULL) {
            return;
        }
        p->ax = nx - x;
        p->ay = ny - y;
        p->b = 0.012f;
        p->color = color;
        p->x0 = minf(x, nx) - GLOW_REACH;
        p->x1 = maxf(x, nx) + GLOW_REACH;
        p->y0 = y - GLOW_REACH;
        p->y1 = ny + GLOW_REACH;
        x = nx;
        y = ny;
    }
}

static void build_scene(float t, float alpha) {
    e.part_count = 0;
    e.flash = 0;
    if (alpha <= 0) {
        return;
    }
    switch (e.scene) {
    case SCENE_RAIN:
        add_rain(t, 28, -0.9f, 0.95f, -1.0f, alpha);
        break;
    case SCENE_SNOW:
        for (int i = 0; i < 30; i++) {
            float size = 0.015f + 0.025f * hash01(i + 50);
            float speed = 0.12f + 0.15f * hash01(i + 100);
            float x = lerp(-0.95f, 0.95f, hash01(i)) + 0.05f * sinf(t * 1.5f + 6 * hash01(i + 150));
            float y = -1.1f + fmodf(hash01(i + 200) * 2.2f + t * speed, 2.2f);
            particle_t *p = add_particle(P_DOT, x, y, size);
            if (p != NULL) {
                p->a = size;
                p->color = scaled(WHITE, 0.85f * alpha);
            }
        }
        break;
    case SCENE_SUN: {
        particle_t *p = add_particle(P_SUN, 0.46f, -0.6f, 0.34f);
        if (p != NULL) {
            p->a = 0.1f + 0.006f * sinf(t * 2);
            p->ax = t * 0.3f;
            p->color = scaled((rgb_t){1.0f, 0.8f, 0.35f}, alpha);
        }
        break;
    }
    case SCENE_STORM: {
        particle_t *p = add_particle(P_CLOUD, 0, -0.64f, 0.42f);
        if (p != NULL) {
            p->color = scaled((rgb_t){0.55f, 0.58f, 0.68f}, alpha);
        }
        add_rain(t, 16, -0.38f, 0.38f, -0.5f, alpha);
        add_bolt(t, alpha);
        break;
    }
    case SCENE_CONFETTI:
        for (int i = 0; i < 36; i++) {
            float speed = 0.3f + 0.25f * hash01(i + 100);
            float x = lerp(-0.95f, 0.95f, hash01(i)) + 0.08f * sinf(t * 2 + 6 * hash01(i + 150));
            float y = -1.1f + fmodf(hash01(i + 200) * 2.3f + t * speed, 2.3f);
            float angle = t * (2 + 3 * hash01(i + 250)) + 6 * hash01(i + 300);
            float flip = fabsf(cosf(t * (3 + 3 * hash01(i + 350))));
            particle_t *p = add_particle(P_RECT, x, y, 0.04f);
            if (p != NULL) {
                p->ax = cosf(angle);
                p->ay = sinf(angle);
                p->a = 0.006f + 0.024f * flip;
                p->b = 0.014f;
                p->color = scaled(CONFETTI_COLORS[i % 5], alpha);
            }
        }
        break;
    case SCENE_SPARKLES:
        for (int i = 0; i < 18; i++) {
            float x = lerp(-0.75f, 0.75f, hash01(i));
            float y = lerp(-0.75f, 0.75f, hash01(i + 50));
            float s = 0.5f + 0.5f * sinf(t * 2.5f * (0.6f + hash01(i + 100)) + 6 * hash01(i + 150));
            float size = 0.11f * s * s * (0.6f + 0.4f * hash01(i + 200));
            if (size < 0.01f) {
                continue;
            }
            particle_t *p = add_particle(P_STAR, x, y, size);
            if (p != NULL) {
                p->a = size;
                rgb_t gold = {1.0f, 0.9f, 0.55f};
                p->color = scaled((i % 2) ? gold : WHITE, alpha);
            }
        }
        break;
    case SCENE_ZZZ:
        for (int i = 0; i < 3; i++) {
            float age = fmodf(t / 3.0f + i / 3.0f, 1.0f);
            float size = 0.03f + 0.05f * age;
            float x = 0.3f + 0.3f * age;
            float y = -0.3f - 0.4f * age;
            particle_t *p = add_particle(P_ZED, x, y, size + GLOW_REACH);
            if (p != NULL) {
                p->a = size;
                p->b = 0.004f + 0.006f * age;
                p->color = scaled(WHITE, sinf(age * 3.14159f) * alpha);
            }
        }
        break;
    }
}

static float fade(uint32_t start, uint32_t end) {
    float age = (e.now - start) / 1000.0f;
    float left = ((int32_t)(end - e.now)) / 1000.0f;
    return minf(smoothstep(0, FADE_IN, age), smoothstep(0, FADE_OUT, left));
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
    int scene_on = e.content == CONTENT_SCENE;
    float scene_t = (now - e.content_start) / 1000.0f;

    // Shape: the expression scaled by intensity, or the scene's mood.
    face_shape_t target = lerp_face(FACES[EXPR_IDLE], FACES[e.state], e.intensity);
    if (scene_on) {
        target = MOODS[e.mood];
    }
    if (scene_on && e.scene == SCENE_ZZZ) {  // drowsy lids
        target.left.lid = maxf(target.left.lid, 0.6f);
        target.right.lid = maxf(target.right.lid, 0.6f);
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

    e.eyes_alpha = 1;
    if (showing_card()) {
        e.eyes_alpha = 1 - e.content_alpha;
    }
    if (scene_on) {
        build_scene(scene_t, e.content_alpha);
    } else {
        e.part_count = 0;
        e.flash = 0;
    }
}

// --- Shapes, as signed distances ---

static float pill(float x, float y, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(x - cx) - hw;
    float qy = fabsf(y - cy) - hh;
    return len(maxf(qx, 0), maxf(qy, 0)) + minf(maxf(qx, qy), 0) - r;
}

static float segment(float x, float y, float ax, float ay, float bx, float by, float w) {
    float px = x - ax;
    float py = y - ay;
    float dx = bx - ax;
    float dy = by - ay;
    float h = clampf((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1);
    return len(px - dx * h, py - dy * h) - w;
}

static float ring_sdf(float x, float y, float cx, float cy, float r, float w) {
    return fabsf(len(x - cx, y - cy) - r) - w;
}

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

// iq's five-point star, y up.
static float star_sdf(float x, float y, float r, float rf) {
    const float k1x = 0.809016994f;
    const float k1y = -0.587785252f;
    x = fabsf(x);
    float d1 = maxf(k1x * x + k1y * y, 0);
    x -= 2 * d1 * k1x;
    y -= 2 * d1 * k1y;
    float d2 = maxf(-k1x * x + k1y * y, 0);
    x -= 2 * d2 * -k1x;
    y -= 2 * d2 * k1y;
    x = fabsf(x);
    y -= r;
    float bax = rf * -k1y;
    float bay = rf * k1x - 1;
    float h = clampf((x * bax + y * bay) / (bax * bax + bay * bay), 0, r);
    float dist = len(x - bax * h, y - bay * h);
    if (y * bax - x * bay < 0) {
        return -dist;
    }
    return dist;
}

// iq's heart, y up, spanning about 0..1.
static float heart_sdf(float x, float y) {
    x = fabsf(x);
    if (y + x > 1) {
        return len(x - 0.25f, y - 0.75f) - 0.35355f;
    }
    float a = len(x, y - 1);
    float m = 0.5f * maxf(x + y, 0);
    float b = len(x - m, y - m);
    float d = minf(a, b);
    if (x < y) {
        return -d;
    }
    return d;
}

static float rbox(float x, float y, float cx, float cy, float hw, float hh, float r) {
    return pill(x, y, cx, cy, hw - r, hh - r, r);
}

// Line icons in icon space (x right, y down, about -1..1).
static float icon_sdf(int icon, float x, float y) {
    const float w = 0.07f;
    float d = 1;
    switch (icon) {
    case ICON_MAIL:
        d = fabsf(rbox(x, y, 0, 0, 0.8f, 0.55f, 0.08f)) - w;
        d = minf(d, segment(x, y, -0.7f, -0.45f, 0, 0.1f, w));
        d = minf(d, segment(x, y, 0, 0.1f, 0.7f, -0.45f, w));
        break;
    case ICON_CALENDAR:
        d = fabsf(rbox(x, y, 0, 0.08f, 0.72f, 0.64f, 0.1f)) - w;
        d = minf(d, segment(x, y, -0.72f, -0.24f, 0.72f, -0.24f, w));
        d = minf(d, segment(x, y, -0.36f, -0.78f, -0.36f, -0.44f, w));
        d = minf(d, segment(x, y, 0.36f, -0.78f, 0.36f, -0.44f, w));
        for (int row = 0; row < 2; row++) {
            for (int col = -1; col <= 1; col++) {
                d = minf(d, len(x - 0.36f * col, y - 0.12f - 0.28f * row) - 0.075f);
            }
        }
        break;
    case ICON_CHECK:
        d = segment(x, y, -0.6f, 0.0f, -0.18f, 0.42f, 0.09f);
        d = minf(d, segment(x, y, -0.18f, 0.42f, 0.62f, -0.45f, 0.09f));
        break;
    case ICON_CROSS:
        d = segment(x, y, -0.5f, -0.5f, 0.5f, 0.5f, 0.09f);
        d = minf(d, segment(x, y, -0.5f, 0.5f, 0.5f, -0.5f, 0.09f));
        break;
    case ICON_WARNING:
        d = segment(x, y, 0, -0.75f, 0.82f, 0.65f, w);
        d = minf(d, segment(x, y, 0.82f, 0.65f, -0.82f, 0.65f, w));
        d = minf(d, segment(x, y, -0.82f, 0.65f, 0, -0.75f, w));
        d = minf(d, segment(x, y, 0, -0.25f, 0, 0.16f, w));
        d = minf(d, len(x, y - 0.4f) - 0.08f);
        break;
    case ICON_CLOCK:
        d = ring_sdf(x, y, 0, 0, 0.75f, w);
        d = minf(d, segment(x, y, 0, 0, 0, -0.45f, w));
        d = minf(d, segment(x, y, 0, 0, 0.32f, 0.14f, w));
        break;
    case ICON_BELL:
        if (y < -0.15f) {
            d = ring_sdf(x, y, 0, -0.15f, 0.45f, w);
        }
        d = minf(d, segment(x, y, -0.45f, -0.15f, -0.58f, 0.42f, w));
        d = minf(d, segment(x, y, 0.45f, -0.15f, 0.58f, 0.42f, w));
        d = minf(d, segment(x, y, -0.72f, 0.42f, 0.72f, 0.42f, w));
        d = minf(d, len(x, y - 0.62f) - 0.11f);
        d = minf(d, len(x, y + 0.68f) - 0.07f);
        break;
    case ICON_CHAT:
        d = fabsf(rbox(x, y, 0, -0.12f, 0.8f, 0.52f, 0.3f)) - w;
        d = minf(d, segment(x, y, -0.4f, 0.4f, -0.5f, 0.75f, w));
        d = minf(d, segment(x, y, -0.5f, 0.75f, 0.0f, 0.4f, w));
        for (int col = -1; col <= 1; col++) {
            d = minf(d, len(x - 0.34f * col, y + 0.12f) - 0.08f);
        }
        break;
    case ICON_CODE:
        d = segment(x, y, -0.32f, -0.45f, -0.78f, 0, w);
        d = minf(d, segment(x, y, -0.78f, 0, -0.32f, 0.45f, w));
        d = minf(d, segment(x, y, 0.32f, -0.45f, 0.78f, 0, w));
        d = minf(d, segment(x, y, 0.78f, 0, 0.32f, 0.45f, w));
        d = minf(d, segment(x, y, 0.15f, -0.6f, -0.15f, 0.6f, w));
        break;
    case ICON_MUSIC:
        d = len(x + 0.45f, y - 0.5f) - 0.18f;
        d = minf(d, len(x - 0.45f, y - 0.35f) - 0.18f);
        d = minf(d, segment(x, y, -0.29f, 0.5f, -0.29f, -0.45f, w));
        d = minf(d, segment(x, y, 0.61f, 0.35f, 0.61f, -0.6f, w));
        d = minf(d, segment(x, y, -0.29f, -0.45f, 0.61f, -0.6f, 0.1f));
        break;
    case ICON_STAR:
        d = fabsf(star_sdf(x, -y + 0.06f, 0.85f, 0.45f)) - w;
        break;
    case ICON_HEART:
        d = fabsf(heart_sdf(x / 1.45f, -y / 1.45f + 0.55f) * 1.45f) - w;
        break;
    }
    return d;
}

// --- Rendering ---

static void add_color(rgb_t *col, rgb_t c, float k) {
    col->r += c.r * k;
    col->g += c.g * k;
    col->b += c.b * k;
}

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

static float particle_lit(const particle_t *p, float u, float v) {
    float dx = u - p->x;
    float dy = v - p->y;
    switch (p->kind) {
    case P_DOT: {
        float d = len(dx, dy);
        return smoothstep(p->a, p->a * 0.3f, d);
    }
    case P_STREAK: {
        float d = segment(u, v, p->x, p->y, p->x + p->ax, p->y + p->ay, p->b);
        return smoothstep(PX, -PX, d);
    }
    case P_BOLT: {
        float d = segment(u, v, p->x, p->y, p->x + p->ax, p->y + p->ay, p->b);
        return glow(d) * 1.5f;
    }
    case P_RECT: {
        float lx = dx * p->ax + dy * p->ay;
        float ly = -dx * p->ay + dy * p->ax;
        float d = pill(lx, ly, 0, 0, p->a, p->b, 0);
        return smoothstep(PX, -PX, d);
    }
    case P_STAR: {
        float ax = fabsf(dx) / p->a;
        float ay = fabsf(dy) / p->a;
        float arm1 = maxf(0, 1 - ax * 8 - ay);
        float arm2 = maxf(0, 1 - ay * 8 - ax);
        float core = maxf(0, 1 - len(ax, ay) * 3);
        return minf(arm1 + arm2 + core, 1);
    }
    case P_ZED: {
        float a = p->a;
        float d = segment(u, v, p->x - a, p->y - a, p->x + a, p->y - a, p->b);
        d = minf(d, segment(u, v, p->x + a, p->y - a, p->x - a, p->y + a, p->b));
        d = minf(d, segment(u, v, p->x - a, p->y + a, p->x + a, p->y + a, p->b));
        return glow(d);
    }
    case P_SUN: {
        float r = len(dx, dy);
        float d = r - p->a;
        float lit = smoothstep(PX, -PX, d) + 0.5f * expf(-maxf(d, 0) * 14);
        if (d > 0.04f && d < 0.15f) {
            float angle = atan2f(dy, dx) + p->ax;
            float ray = smoothstep(0.8f, 0.95f, cosf(angle * 10));
            float reach = smoothstep(0.04f, 0.06f, d) * smoothstep(0.15f, 0.12f, d);
            lit += ray * reach;
        }
        return lit;
    }
    case P_CLOUD: {
        float d = len(dx + 0.2f, dy - 0.04f) - 0.15f;
        d = minf(d, len(dx, dy + 0.04f) - 0.2f);
        d = minf(d, len(dx - 0.21f, dy - 0.05f) - 0.14f);
        d = minf(d, rbox(dx, dy, 0, 0.1f, 0.33f, 0.09f, 0.09f));
        float fill = 0.35f * smoothstep(PX, -PX, d);
        float edge = smoothstep(0.012f, 0.0f, fabsf(d));
        return fill + edge;
    }
    }
    return 0;
}

static float avatar_channel(uint16_t p, int shift, int bits) {
    int max = (1 << bits) - 1;
    return ((p >> shift) & max) / (float)max;
}

static rgb_t avatar_pixel(int x, int y) {
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    if (x >= AVATAR_SIZE) {
        x = AVATAR_SIZE - 1;
    }
    if (y >= AVATAR_SIZE) {
        y = AVATAR_SIZE - 1;
    }
    uint16_t p = avatar[y * AVATAR_SIZE + x];
    rgb_t c = {avatar_channel(p, 11, 5), avatar_channel(p, 5, 6), avatar_channel(p, 0, 5)};
    return c;
}

static rgb_t avatar_sample(float gx, float gy) {
    int ix = (int)floorf(gx);
    int iy = (int)floorf(gy);
    float fx = gx - ix;
    float fy = gy - iy;
    rgb_t a = avatar_pixel(ix, iy);
    rgb_t b = avatar_pixel(ix + 1, iy);
    rgb_t c = avatar_pixel(ix, iy + 1);
    rgb_t d = avatar_pixel(ix + 1, iy + 1);
    rgb_t out;
    out.r = lerp(lerp(a.r, b.r, fx), lerp(c.r, d.r, fx), fy);
    out.g = lerp(lerp(a.g, b.g, fx), lerp(c.g, d.g, fx), fy);
    out.b = lerp(lerp(a.b, b.b, fx), lerp(c.b, d.b, fx), fy);
    return out;
}

// Card content at screen point (u, v), added to col.
static void card_color(rgb_t *col, float u, float v, float px, float py, float alpha) {
    float dy = v - CARD_Y;
    float r = len(u, dy);
    if (r > CARD_R + GLOW_REACH) {
        return;
    }
    if (e.content == CONTENT_ICON) {
        float scale = 0.42f;
        float d = icon_sdf(e.icon, u / scale, dy / scale) * scale;
        if (d < GLOW_REACH) {
            add_color(col, ICON_COLORS[e.icon], glow(d) * alpha);
        }
        return;
    }
    float ring = ring_sdf(u, dy, 0, 0, CARD_R, 0.008f);
    add_color(col, WHITE, glow(ring) * 0.8f * alpha);
    if (e.content == CONTENT_INITIALS) {
        add_color(col, WHITE, text_alpha(&e.initials, px, py) * alpha);
        return;
    }
    float inner = CARD_R - 0.02f;
    if (r < inner + PX) {
        float edge = smoothstep(inner + PX, inner - PX, r);
        float gx = (u / inner + 1) * AVATAR_SIZE / 2 - 0.5f;
        float gy = (dy / inner + 1) * AVATAR_SIZE / 2 - 0.5f;
        rgb_t c = avatar_sample(gx, gy);
        add_color(col, c, edge * alpha);
    }
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

static void render_row(uint16_t *out, int j) {
    float v = (j + 0.5f) / HALF - 1;
    float py = j + 0.5f;

    // Particles that touch this row.
    const particle_t *row_parts[MAX_PARTICLES];
    int row_count = 0;
    for (int k = 0; k < e.part_count; k++) {
        if (v >= e.parts[k].y0 && v <= e.parts[k].y1) {
            row_parts[row_count] = &e.parts[k];
            row_count++;
        }
    }
    int eyes_row = e.eyes_alpha > 0 && v >= e.ev0 && v <= e.ev1;
    int label_row = e.label.count > 0 && py >= e.label.y0 && py <= e.label.y1;
    int card_row = showing_card() && fabsf(v - CARD_Y) < CARD_R + GLOW_REACH;

    for (int i = 0; i < EYE_SIZE; i++) {
        float u = (i + 0.5f) / HALF - 1;
        float r2 = u * u + v * v;
        if (r2 >= 1) {
            out[i] = 0;
            continue;
        }

        // Black glass: faint top-left sheen and a thin rim of light so the sphere reads.
        float bg = 0.015f + e.flash;
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

        for (int k = 0; k < row_count; k++) {
            const particle_t *p = row_parts[k];
            if (u < p->x0 || u > p->x1) {
                continue;
            }
            add_color(&col, p->color, particle_lit(p, u, v));
        }

        if (eyes_row && u >= e.eu0 && u <= e.eu1) {
            float lit = eyes_lit(u, v, sqrtf(1 - r2));
            add_color(&col, WHITE, lit * e.eyes_alpha);
        }

        if (card_row) {
            card_color(&col, u, v, i + 0.5f, py, e.content_alpha);
        }

        if (label_row) {
            float a = text_alpha(&e.label, i + 0.5f, py);
            add_color(&col, WHITE, a * e.content_alpha);
        }

        out[i] = rgb565(col, i, j);
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
