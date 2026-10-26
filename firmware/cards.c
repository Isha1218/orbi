// Glance cards. Each icon is a small glowing illustration drawn with signed distances in
// "icon units" (x right, y down, about -1..1), with its own entrance and idle loop.
#include "card.h"
#include "sprites.h"
#include "text.h"
#include <string.h>

#define ICON_SCALE 0.5f   // screen size of one icon unit
#define REACH 1.4f        // how far the drawing and its glow extend, icon units

static struct {
    int kind, icon;
    float t, left;
    float fade;           // whole card in and out
    float scale;          // pop
    float sx, sy;         // squash and stretch
    float ox, oy;         // offset in screen units
    float reach;          // drawing bounds, icon units
    float aa;             // one pixel in icon units
    rgb_t accent;
    int split;            // sprites before this are behind the illustration

    // animation values, each used by one or two icons
    float flap, letter;   // mail
    float flip;           // calendar
    float draw, shake;    // check, cross
    float shine;          // check, star: position of the sweeping highlight
    float swing, clapper; // bell, clock wobble
    float hand_h, hand_m, hand_s;
    float pulse;          // warning glow, heart beat
    float ring;           // warning, heart: expanding outline 0..1
    float bang;           // warning: the "!" wobble
    float typed, cursor;  // code
    float dots[3];        // chat typing dots
    float beat;           // music
    float spin;           // star
    float text_pop;       // initials
    float ray_c, ray_s;   // rotation of the background rays
    float turn_c, turn_s; // the icon's main rotation, from swing, shake or spin
    float part_c, part_s; // a second rotation: the bell's clapper, the warning's "!"
} cd;

static text_t initials_text;
static float initials_hue;
static uint16_t avatar[AVATAR_SIZE * AVATAR_SIZE];
static rgb_t avatar_mean = {0.6f, 0.75f, 1.0f};

static const rgb_t WHITE = {1, 1, 1};

static const rgb_t ACCENTS[ICON_COUNT] = {
    [ICON_MAIL] = {0.45f, 0.65f, 1.0f},
    [ICON_CALENDAR] = {1.0f, 0.45f, 0.45f},
    [ICON_CHECK] = {0.3f, 1.0f, 0.55f},
    [ICON_CROSS] = {1.0f, 0.3f, 0.35f},
    [ICON_WARNING] = {1.0f, 0.7f, 0.2f},
    [ICON_CLOCK] = {0.35f, 0.75f, 1.0f},
    [ICON_BELL] = {1.0f, 0.8f, 0.3f},
    [ICON_CHAT] = {0.4f, 0.65f, 1.0f},
    [ICON_CODE] = {0.3f, 0.95f, 0.85f},
    [ICON_MUSIC] = {0.8f, 0.45f, 1.0f},
    [ICON_STAR] = {1.0f, 0.82f, 0.3f},
    [ICON_HEART] = {1.0f, 0.35f, 0.55f},
};

// --- Drawing helpers, in icon units ---

static float cover(float d) {
    return fill(d, cd.aa);
}

static void paint(rgb_t *col, rgb_t c, float a) {
    over(col, c, a * cd.fade);
}

static void light(rgb_t *col, rgb_t c, float k) {
    add_color(col, c, k * cd.fade);
}

// Soft light around a shape, fading to nothing at reach.
static void glow_of(rgb_t *col, float d, rgb_t c, float strength, float reach) {
    if (d <= 0 || d >= reach) {
        return;
    }
    float k = 1 - d / reach;
    light(col, c, strength * k * k);
}

static rgb_t vgrad(rgb_t top, rgb_t bottom, float y, float y0, float y1) {
    return mix(top, bottom, sat((y - y0) / (y1 - y0)));
}

// Soft oval highlight, like a reflection on glossy plastic.
static void gloss(rgb_t *col, float x, float y, float cx, float cy, float rx, float ry, float k, float inside) {
    float e = len((x - cx) / rx, (y - cy) / ry);
    float g = sat(1 - e);
    light(col, WHITE, g * g * k * inside);
}

// Darkens toward the inside edge of a shape so it looks rounded.
static rgb_t rounded(rgb_t c, float d, float depth, float amount) {
    float k = smoothstep(-depth, 0, d);
    return scaled(c, 1 - amount * k);
}

// Turns a point about (cx, cy) by an angle given as its cosine and sine.
static void rotate_about(float *x, float *y, float cx, float cy, float c, float s) {
    float dx = *x - cx;
    float dy = *y - cy;
    *x = cx + dx * c + dy * s;
    *y = cy - dx * s + dy * c;
}

// Distance to the first part (0..1) of the stroke a -> b -> c, so it can draw itself.
static float partial_stroke(float x, float y, const float *p, float amount, float w) {
    float l1 = len(p[2] - p[0], p[3] - p[1]);
    float l2 = len(p[4] - p[2], p[5] - p[3]);
    float s = amount * (l1 + l2);
    if (s <= 0.001f) {
        return 10;
    }
    if (s <= l1) {
        float k = s / l1;
        return segment(x, y, p[0], p[1], lerp(p[0], p[2], k), lerp(p[1], p[3], k), w);
    }
    float k = (s - l1) / l2;
    float d = segment(x, y, p[0], p[1], p[2], p[3], w);
    return minf(d, segment(x, y, p[2], p[3], lerp(p[2], p[4], k), lerp(p[3], p[5], k), w));
}

// Screen position of an icon-unit point.
static float screen_x(float x) {
    return cd.ox + x * ICON_SCALE * cd.scale;
}

static float screen_y(float y) {
    return CARD_Y + cd.oy + y * ICON_SCALE * cd.scale;
}

// Glossy colored disc used by check, cross and initials. Returns its coverage.
static float draw_disc(rgb_t *col, float x, float y, float r, rgb_t top, rgb_t bottom, float glow) {
    float d = len(x, y) - r;
    glow_of(col, d, cd.accent, glow, 0.5f);
    float a = cover(d);
    if (a <= 0) {
        return 0;
    }
    rgb_t c = mix(top, bottom, sat(0.5f + (x + y) * 0.5f / r));
    c = rounded(c, d, 0.18f, 0.35f);
    paint(col, c, a);
    gloss(col, x, y, -0.15f * r, -0.55f * r, 0.6f * r, 0.32f * r, 0.35f, a);
    // a highlight band sweeps across now and then
    float band = (x + y) * 0.7f - cd.shine;
    light(col, WHITE, 0.25f * a / (1 + band * band * 60));
    return a;
}

// --- Mail: the flap opens and a letter slides up ---

static void draw_mail(rgb_t *col, float x, float y) {
    rgb_t env_top = {0.66f, 0.8f, 1.0f};
    rgb_t env_bottom = {0.34f, 0.48f, 0.95f};
    rgb_t inside = {0.08f, 0.12f, 0.3f};
    rgb_t paper = {0.98f, 0.98f, 1.0f};
    float body = rbox(x, y, 0, 0.25f, 0.86f, 0.56f, 0.1f);
    glow_of(col, body, cd.accent, 0.35f, 0.45f);
    float open = sat((1 - cosf(cd.flap)) / 2);
    float fc = cosf(cd.flap);
    float tip = -0.31f + 0.66f * fc;

    float a = cover(body);
    if (a > 0) {
        paint(col, mix(vgrad(env_top, env_bottom, y, -0.3f, 0.8f), inside, open * 0.9f), a);
    }
    float flap = 10;
    if (fabsf(fc) > 0.03f) {
        flap = triangle(x, y, -0.86f, -0.31f, 0.86f, -0.31f, 0, tip) - 0.03f;
    }
    if (fc < 0) {
        rgb_t back = vgrad(rgb(0.42f, 0.55f, 0.95f), rgb(0.3f, 0.4f, 0.85f), y, tip, -0.31f);
        paint(col, back, cover(flap));
    }

    // the letter, kept inside the envelope at the bottom
    float top = 0.12f - 0.9f * cd.letter;
    float letter = rbox(x, y, 0, top + 0.45f, 0.68f, 0.45f, 0.06f);
    letter = maxf(letter, y - 0.72f);
    float la = cover(letter);
    if (la > 0) {
        paint(col, vgrad(paper, rgb(0.86f, 0.89f, 0.97f), y, top, top + 0.9f), la);
        float lines = segment(x, y, -0.46f, top + 0.24f, 0.18f, top + 0.24f, 0.035f);
        lines = minf(lines, segment(x, y, -0.46f, top + 0.42f, 0.46f, top + 0.42f, 0.035f));
        lines = minf(lines, segment(x, y, -0.46f, top + 0.6f, 0.2f, top + 0.6f, 0.035f));
        paint(col, rgb(0.6f, 0.68f, 0.88f), cover(lines) * la);
        float stamp = heart(x - 0.44f, y - top - 0.22f, 0.1f);
        paint(col, rgb(1, 0.35f, 0.5f), cover(stamp) * la);
    }

    // front pocket: a V-shaped fold in front of the letter
    float edge = 0.04f + 0.3f * (1 - fabsf(x) / 0.86f);
    float pocket = maxf(body, edge - y);
    float pa = cover(pocket);
    if (pa > 0) {
        rgb_t c = vgrad(env_top, env_bottom, y, edge, 0.8f);
        float fold = minf(segment(x, y, -0.86f, 0.8f, 0, 0.36f, 0.006f), segment(x, y, 0.86f, 0.8f, 0, 0.36f, 0.006f));
        c = scaled(c, 1 - 0.18f * cover(fold));
        float lip = sat(1 - (y - edge) / 0.05f);
        add_color(&c, WHITE, 0.25f * lip);
        paint(col, c, pa);
    }
    if (fc >= 0) {
        float fa = cover(flap);
        if (fa > 0) {
            rgb_t c = vgrad(rgb(0.72f, 0.84f, 1.0f), rgb(0.48f, 0.62f, 0.98f), y, -0.31f, tip);
            float lip = smoothstep(-0.06f, 0, flap);
            add_color(&c, WHITE, 0.2f * lip);
            paint(col, c, fa);
        }
    }
}

// --- Calendar: a page with a red header that flips over ---

static void calendar_page(rgb_t *col, float x, float y, float shade, float alpha) {
    float page = rbox(x, y, 0, 0.1f, 0.72f, 0.68f, 0.12f);
    float a = cover(page) * alpha;
    if (a <= 0) {
        return;
    }
    rgb_t c;
    if (y < -0.24f) {
        c = vgrad(rgb(1, 0.48f, 0.45f), rgb(0.88f, 0.24f, 0.32f), y, -0.58f, -0.24f);
        float title = segment(x, y, -0.25f, -0.36f, 0.25f, -0.36f, 0.045f);
        c = mix(c, rgb(1, 0.92f, 0.92f), cover(title) * 0.9f);
    } else {
        c = vgrad(WHITE, rgb(0.84f, 0.87f, 0.95f), y, -0.24f, 0.78f);
        float col_i = floorf((x + 0.6f) / 0.3f);
        float row_i = floorf((y + 0.12f) / 0.24f);
        if (col_i >= 0 && col_i < 4 && row_i >= 0 && row_i < 3) {
            float cx = -0.45f + 0.3f * col_i;
            float cy = 0.0f + 0.24f * row_i;
            if (col_i == 2 && row_i == 1) {
                float r = 0.12f * (1 + 0.08f * cd.pulse);
                float dot = len(x - cx, y - cy) - r;
                c = mix(c, rgb(1, 0.38f, 0.42f), cover(dot));
                c = mix(c, WHITE, cover(len(x - cx, y - cy) - 0.035f));
            } else {
                float cell = rbox(x, y, cx, cy, 0.09f, 0.07f, 0.03f);
                c = mix(c, rgb(0.78f, 0.82f, 0.92f), cover(cell));
            }
        }
    }
    c = rounded(c, page, 0.08f, 0.2f);
    paint(col, scaled(c, shade), a);
}

static void draw_calendar(rgb_t *col, float x, float y) {
    const float hinge = -0.58f;
    const float height = 1.36f;
    float page = rbox(x, y, 0, 0.1f, 0.72f, 0.68f, 0.12f);
    glow_of(col, page, cd.accent, 0.3f, 0.45f);
    float under = rbox(x, y, 0.06f, 0.16f, 0.72f, 0.68f, 0.12f);
    paint(col, rgb(0.55f, 0.58f, 0.7f), cover(under));
    calendar_page(col, x, y, 1, 1);

    float c = cosf(cd.flip);
    float s = sinf(cd.flip);
    if (c > 0.02f && y > hinge && y < hinge + height * c) {
        // the top sheet, tilting up toward us: squashed vertically, a little wider at the bottom
        float yy = hinge + (y - hinge) / c;
        float xx = x / (1 + 0.25f * s * (yy - hinge) / height);
        calendar_page(col, xx, yy, 1 - 0.35f * s, 1);
    } else if (c < -0.02f && y < hinge && y > hinge + height * c) {
        // past the top it shows its back, and fades as it falls behind
        float yy = hinge + (hinge - y) / -c;
        float xx = x / (1 + 0.25f * s * (yy - hinge) / height);
        float back = rbox(xx, yy, 0, 0.1f, 0.72f, 0.68f, 0.12f);
        float gone = sat((PI - cd.flip) / 0.9f);
        paint(col, scaled(rgb(0.85f, 0.87f, 0.95f), 0.6f + 0.4f * -c), cover(back) * gone);
    }

    for (int k = -1; k <= 1; k += 2) {
        float hole = len(x - 0.36f * k, y + 0.47f) - 0.06f;
        paint(col, rgb(0.35f, 0.08f, 0.12f), cover(hole));
        float ring = rbox(x, y, 0.36f * k, -0.6f, 0.05f, 0.15f, 0.05f);
        rgb_t metal = mix(rgb(0.95f, 0.96f, 1.0f), rgb(0.5f, 0.55f, 0.68f), sat((x - 0.36f * k + 0.05f) * 10));
        paint(col, metal, cover(ring));
    }
}

// --- Check and cross: a disc pops in, the mark draws itself ---

static void draw_check(rgb_t *col, float x, float y) {
    static const float STROKE[6] = {-0.3f, 0.02f, -0.08f, 0.24f, 0.33f, -0.22f};
    if (draw_disc(col, x, y, 0.62f, rgb(0.5f, 1.0f, 0.62f), rgb(0.04f, 0.6f, 0.36f), 0.45f) <= 0) {
        return;
    }
    float shadow = partial_stroke(x, y - 0.04f, STROKE, cd.draw, 0.09f);
    paint(col, rgb(0.02f, 0.35f, 0.18f), cover(shadow) * 0.4f);
    float d = partial_stroke(x, y, STROKE, cd.draw, 0.09f);
    paint(col, WHITE, cover(d));
}

static void draw_cross(rgb_t *col, float x, float y) {
    static const float STROKE1[6] = {-0.25f, -0.25f, 0, 0, 0.25f, 0.25f};
    static const float STROKE2[6] = {0.25f, -0.25f, 0, 0, -0.25f, 0.25f};
    x -= cd.shake;
    rotate_about(&x, &y, 0, 0, cd.turn_c, cd.turn_s);
    if (draw_disc(col, x, y, 0.62f, rgb(1, 0.5f, 0.45f), rgb(0.72f, 0.08f, 0.18f), 0.45f) <= 0) {
        return;
    }
    float a1 = sat(cd.draw * 2);
    float a2 = sat(cd.draw * 2 - 1);
    float shadow = minf(partial_stroke(x, y - 0.04f, STROKE1, a1, 0.085f), partial_stroke(x, y - 0.04f, STROKE2, a2, 0.085f));
    paint(col, rgb(0.4f, 0.02f, 0.08f), cover(shadow) * 0.4f);
    float d = minf(partial_stroke(x, y, STROKE1, a1, 0.085f), partial_stroke(x, y, STROKE2, a2, 0.085f));
    paint(col, WHITE, cover(d));
}

// --- Warning: an amber sign drops in, glows and sends out pulses ---

static float warning_shape(float x, float y) {
    return triangle(x, y, 0, -0.6f, 0.74f, 0.6f, -0.74f, 0.6f) - 0.1f;
}

static void draw_warning(rgb_t *col, float x, float y) {
    rgb_t amber = {1.0f, 0.68f, 0.18f};
    if (cd.ring > 0 && cd.ring < 1) {
        float k = 1 + 0.75f * cd.ring;
        float d = fabsf(warning_shape(x / k, (y - 0.2f) / k + 0.2f) * k) - 0.025f;
        light(col, amber, cover(d) * (1 - cd.ring) * 0.8f);
    }
    float d = warning_shape(x, y);
    glow_of(col, d, amber, 0.35f + 0.3f * cd.pulse, 0.55f);
    float a = cover(d);
    if (a <= 0) {
        return;
    }
    rgb_t c = vgrad(rgb(1, 0.9f, 0.4f), rgb(1, 0.52f, 0.1f), y, -0.6f, 0.65f);
    c = rounded(c, d, 0.12f, 0.3f);
    float bevel = smoothstep(-0.1f, -0.06f, d) * (1 - smoothstep(-0.05f, -0.02f, d));
    add_color(&c, WHITE, 0.25f * bevel);
    paint(col, c, a);
    gloss(col, x, y, -0.12f, -0.15f, 0.25f, 0.4f, 0.25f, a);
    float bx = x;
    float by = y;
    rotate_about(&bx, &by, 0, 0.4f, cd.part_c, cd.part_s);
    float mark = segment(bx, by, 0, -0.26f, 0, 0.14f, 0.075f - 0.02f * sat((by + 0.26f) / 0.4f));
    mark = minf(mark, len(bx, by - 0.36f) - 0.075f);
    paint(col, rgb(0.3f, 0.13f, 0.02f), cover(mark));
}

// --- Clock: an alarm clock that spins up to time and ticks ---

static void hand_dir(float angle, float *dx, float *dy) {
    *dx = sinf(angle);
    *dy = -cosf(angle);
}

static void draw_clock(rgb_t *col, float x, float y) {
    rgb_t metal_top = {0.55f, 0.85f, 1.0f};
    rgb_t metal_bottom = {0.15f, 0.38f, 0.85f};
    rotate_about(&x, &y, 0, 0.75f, cd.turn_c, cd.turn_s);
    float cy = 0.08f;
    float body = len(x, y - cy) - 0.66f;
    float bells = minf(len(x + 0.44f, y + 0.6f) - 0.2f, len(x - 0.44f, y + 0.6f) - 0.2f);
    bells = maxf(bells, y + 0.5f);
    float legs = minf(segment(x, y, -0.36f, 0.62f, -0.48f, 0.84f, 0.05f), segment(x, y, 0.36f, 0.62f, 0.48f, 0.84f, 0.05f));
    float hammer = segment(x, y, 0, -0.58f, 0, -0.74f, 0.03f);
    hammer = minf(hammer, len(x, y + 0.76f) - 0.06f);
    float shape = minf(minf(body, bells), minf(legs, hammer));
    glow_of(col, shape, cd.accent, 0.3f, 0.45f);
    float metal = minf(minf(bells, legs), hammer);
    paint(col, vgrad(metal_top, metal_bottom, y, -0.8f, 0.85f), cover(metal));
    float a = cover(body);
    if (a <= 0) {
        return;
    }
    paint(col, rounded(vgrad(metal_top, metal_bottom, y, -0.6f, 0.75f), body, 0.1f, 0.3f), a);
    float r = len(x, y - cy);
    float face = r - 0.55f;
    rgb_t cream = vgrad(rgb(1, 0.99f, 0.95f), rgb(0.82f, 0.85f, 0.94f), y, -0.45f, 0.65f);
    paint(col, rounded(cream, face, 0.1f, 0.15f), cover(face));
    if (r > 0.36f && r < 0.52f) {
        float angle = atan2f(x, -(y - cy));
        float step = PI / 6;
        float k = roundf(angle / step);
        float across = r * fabsf(angle - k * step);
        float size = 0.03f;
        if (((int)k) % 3 == 0) {
            size = 0.06f;
        }
        float tick = maxf(across - 0.018f, fabsf(r - 0.46f) - size);
        paint(col, rgb(0.25f, 0.3f, 0.45f), cover(tick));
    }
    float dx;
    float dy;
    hand_dir(cd.hand_h, &dx, &dy);
    float hands = segment(x, y, 0, cy, dx * 0.26f, cy + dy * 0.26f, 0.045f);
    hand_dir(cd.hand_m, &dx, &dy);
    hands = minf(hands, segment(x, y, 0, cy, dx * 0.4f, cy + dy * 0.4f, 0.032f));
    paint(col, rgb(0.12f, 0.15f, 0.28f), cover(hands));
    hand_dir(cd.hand_s, &dx, &dy);
    float second = segment(x, y, -dx * 0.1f, cy - dy * 0.1f, dx * 0.46f, cy + dy * 0.46f, 0.013f);
    second = minf(second, len(x, y - cy) - 0.05f);
    paint(col, rgb(1, 0.35f, 0.35f), cover(second));
    gloss(col, x, y, -0.2f, -0.2f, 0.35f, 0.2f, 0.3f, a);
}

// --- Bell: swings, its clapper lagging behind, sending out ring waves ---

static float bell_body(float x, float y) {
    float dome = len(x, y + 0.2f) - 0.42f;
    float half = 0.42f + (y + 0.2f) * 0.3f;
    float skirt = maxf(fabsf(x) - half, maxf(-0.2f - y, y - 0.44f));
    float lip = rbox(x, y, 0, 0.47f, 0.7f, 0.08f, 0.08f);
    return minf(minf(dome, skirt), lip);
}

static void draw_bell(rgb_t *col, float x, float y) {
    rgb_t gold_light = {1.0f, 0.93f, 0.55f};
    rgb_t gold_dark = {0.85f, 0.5f, 0.1f};
    if (fabsf(cd.swing) > 0.05f) {
        // ring waves on both sides
        float r = len(x, y);
        for (int k = 0; k < 2; k++) {
            float p = fmodf(cd.t * 1.8f + k * 0.5f, 1.0f);
            float d = fabsf(r - (0.8f + 0.5f * p)) - 0.03f;
            float side = smoothstep(0.35f, 0.75f, fabsf(x) / (r + 0.001f));
            light(col, gold_light, cover(d) * side * (1 - p) * sat(fabsf(cd.swing) * 4) * 0.8f);
        }
    }
    float cx = x;
    float cy = y;
    rotate_about(&cx, &cy, 0, -0.78f, cd.part_c, cd.part_s);
    float clapper = len(cx, cy - 0.6f) - 0.11f;
    rotate_about(&x, &y, 0, -0.78f, cd.turn_c, cd.turn_s);
    float body = bell_body(x, y);
    float knob = ring_sdf(x, y, 0, -0.7f, 0.08f, 0.035f);
    float shape = minf(minf(body, knob), clapper);
    glow_of(col, shape, cd.accent, 0.35f, 0.5f);
    paint(col, gold_dark, cover(clapper));
    paint(col, gold_dark, cover(knob));
    float a = cover(body);
    if (a <= 0) {
        return;
    }
    rgb_t c = mix(gold_light, gold_dark, sat(0.45f + x * 0.7f + y * 0.2f));
    if (y > 0.39f) {
        c = scaled(c, 0.85f);
    }
    c = rounded(c, body, 0.08f, 0.2f);
    paint(col, c, a);
    float stripe = (x + 0.2f + y * 0.08f) / 0.07f;
    light(col, WHITE, 0.4f * a / (1 + stripe * stripe) * sat(0.6f - y));
}

// --- Chat: a speech bubble with bouncing typing dots, and a reply peeking behind ---

static void draw_chat(rgb_t *col, float x, float y) {
    float small = 10;
    float pop = ease_back((cd.t - 0.55f) / 0.35f);
    if (pop > 0) {
        float sx = 0.45f + (x - 0.45f) / pop;
        float sy = -0.56f + (y + 0.56f) / pop;
        small = rbox(sx, sy, 0.45f, -0.56f, 0.42f, 0.28f, 0.28f) * pop;
        paint(col, vgrad(rgb(0.95f, 0.65f, 1.0f), rgb(0.62f, 0.35f, 0.95f), y, -0.84f, -0.28f), cover(small));
        float mini = len(sx - 0.45f, sy + 0.56f) - 0.06f;
        mini = minf(mini, minf(len(sx - 0.27f, sy + 0.56f), len(sx - 0.63f, sy + 0.56f)) - 0.05f);
        paint(col, rgb(1, 0.9f, 1), cover(mini * pop) * 0.85f);
    }
    float bubble = rbox(x, y, -0.04f, -0.04f, 0.8f, 0.52f, 0.42f);
    float tail = triangle(x, y, -0.52f, 0.3f, -0.12f, 0.4f, -0.7f, 0.75f) - 0.02f;
    bubble = minf(bubble, tail);
    glow_of(col, minf(bubble, small), cd.accent, 0.35f, 0.45f);
    float a = cover(bubble);
    if (a <= 0) {
        return;
    }
    rgb_t c = vgrad(rgb(0.55f, 0.8f, 1.0f), rgb(0.2f, 0.42f, 0.98f), y, -0.56f, 0.75f);
    c = rounded(c, bubble, 0.12f, 0.25f);
    paint(col, c, a);
    gloss(col, x, y, -0.25f, -0.38f, 0.45f, 0.12f, 0.35f, a);
    for (int k = 0; k < 3; k++) {
        float bounce = cd.dots[k];
        float r = 0.085f * (1 + 0.15f * bounce);
        float dot = len(x - (-0.32f + 0.3f * k), y + 0.04f + 0.14f * bounce) - r;
        paint(col, WHITE, cover(dot) * (0.75f + 0.25f * bounce));
    }
}

// --- Code: a little editor window typing itself, with a blinking cursor ---

typedef struct {
    int indent;
    int len[3];
    int color[3];
} code_line_t;

static const code_line_t CODE[5] = {
    {0, {3, 5, 2}, {0, 1, 4}},
    {2, {2, 6, 0}, {0, 2, 4}},
    {4, {4, 3, 3}, {3, 4, 2}},
    {2, {1, 0, 0}, {4, 0, 0}},
    {0, {5, 4, 0}, {1, 3, 0}},
};

static const rgb_t CODE_COLORS[5] = {
    {1.0f, 0.5f, 0.75f},
    {0.45f, 0.9f, 1.0f},
    {1.0f, 0.88f, 0.45f},
    {0.6f, 1.0f, 0.6f},
    {0.75f, 0.78f, 0.92f},
};

static void draw_code(rgb_t *col, float x, float y) {
    const float cw = 0.075f;  // one character
    float win = rbox(x, y, 0, 0, 0.9f, 0.68f, 0.12f);
    float pop = ease_back((cd.t - 0.3f) / 0.4f);
    glow_of(col, win, cd.accent, 0.3f, 0.45f);
    float a = cover(win);
    if (a > 0) {
        rgb_t c = vgrad(rgb(0.1f, 0.13f, 0.24f), rgb(0.05f, 0.06f, 0.13f), y, -0.68f, 0.68f);
        if (y < -0.45f) {
            c = rgb(0.18f, 0.21f, 0.34f);
        }
        float border = fabsf(win + 0.012f) - 0.012f;
        c = mix(c, rgb(0.4f, 0.6f, 0.95f), cover(border) * 0.7f);
        paint(col, c, a);
        for (int k = 0; k < 3; k++) {
            rgb_t light_colors[3] = {{1, 0.38f, 0.38f}, {1, 0.8f, 0.3f}, {0.35f, 0.9f, 0.45f}};
            float dot = len(x + 0.72f - 0.13f * k, y + 0.56f) - 0.042f;
            paint(col, light_colors[k], cover(dot));
        }
        // find this pixel's line, then its token
        int line = (int)floorf((y + 0.36f) / 0.17f);
        int count = 0;
        float cursor_x = -0.72f;
        int cursor_line = 0;
        for (int l = 0; l < 5; l++) {
            float pen = -0.72f + CODE[l].indent * cw;
            for (int k = 0; k < 3; k++) {
                int n = CODE[l].len[k];
                if (n == 0) {
                    continue;
                }
                float shown = clampf(cd.typed - count, 0, n);
                if (l == line && shown > 0) {
                    float ly = -0.28f + 0.17f * l;
                    float bar = rbox(x, y, pen + shown * cw / 2, ly, shown * cw / 2 - 0.01f, 0.042f, 0.042f);
                    paint(col, CODE_COLORS[CODE[l].color[k]], cover(bar));
                }
                if (cd.typed >= count) {
                    cursor_x = pen + shown * cw;
                    cursor_line = l;
                }
                count += n;
                pen += (n + 1) * cw;
            }
        }
        float caret = rbox(x, y, cursor_x + 0.03f, -0.28f + 0.17f * cursor_line, 0.022f, 0.06f, 0.01f);
        paint(col, rgb(0.9f, 0.95f, 1.0f), cover(caret) * cd.cursor);
    }
    if (pop > 0) {
        float bx = 0.7f + (x - 0.7f) / pop;
        float by = 0.55f + (y - 0.55f) / pop;
        float b = (len(bx - 0.7f, by - 0.55f) - 0.26f) * pop;
        glow_of(col, b, cd.accent, 0.3f, 0.3f);
        float ba = cover(b);
        if (ba > 0) {
            paint(col, vgrad(rgb(0.4f, 1.0f, 0.88f), rgb(0.05f, 0.62f, 0.6f), by, 0.3f, 0.8f), ba);
            float g = segment(bx, by, 0.6f, 0.47f, 0.53f, 0.55f, 0.022f);
            g = minf(g, segment(bx, by, 0.53f, 0.55f, 0.6f, 0.63f, 0.022f));
            g = minf(g, segment(bx, by, 0.8f, 0.47f, 0.87f, 0.55f, 0.022f));
            g = minf(g, segment(bx, by, 0.87f, 0.55f, 0.8f, 0.63f, 0.022f));
            g = minf(g, segment(bx, by, 0.74f, 0.44f, 0.66f, 0.66f, 0.022f));
            paint(col, WHITE, cover(g * pop) * ba);
        }
    }
}

// --- Music: beamed notes bouncing to the beat ---

static float note_head(float x, float y, float cx, float cy) {
    float dx = x - cx;
    float dy = y - cy;
    float rx = (dx * 0.93f + dy * 0.37f) / 0.21f;
    float ry = (-dx * 0.37f + dy * 0.93f) / 0.155f;
    return (len(rx, ry) - 1) * 0.155f;
}

static void draw_music(rgb_t *col, float x, float y) {
    // hop between beats, squash on landing
    float b = cd.beat;
    float hop = 0.12f * sinf(PI * b);
    float squash = 0.14f * expf(-b * 10);
    float base = 0.5f;
    y = base + (y - base + hop) / (1 - squash);
    x = x / (1 + squash);
    float heads = minf(note_head(x, y, -0.4f, 0.4f), note_head(x, y, 0.42f, 0.28f));
    float stems = minf(segment(x, y, -0.22f, 0.36f, -0.22f, -0.44f, 0.045f), segment(x, y, 0.6f, 0.24f, 0.6f, -0.58f, 0.045f));
    float beam = maxf(segment(x, y, -0.22f, -0.5f, 0.6f, -0.64f, 0.09f), fabsf(x - 0.19f) - 0.455f);
    float d = minf(heads, minf(stems, beam));
    glow_of(col, d, cd.accent, 0.4f, 0.5f);
    float a = cover(d);
    if (a <= 0) {
        return;
    }
    rgb_t c = vgrad(rgb(0.72f, 0.5f, 1.0f), rgb(1.0f, 0.42f, 0.72f), y, -0.7f, 0.6f);
    paint(col, c, a);
    gloss(col, x, y, -0.45f, 0.34f, 0.1f, 0.05f, 0.6f, a);
    gloss(col, x, y, 0.37f, 0.22f, 0.1f, 0.05f, 0.6f, a);
    gloss(col, x, y, 0.1f, -0.6f, 0.3f, 0.04f, 0.35f, a);
}

// --- Star: a faceted gold star that spins in, with a glint ---

static void draw_star(rgb_t *col, float x, float y) {
    rotate_about(&x, &y, 0, 0.05f, cd.turn_c, cd.turn_s);
    float d = star_sdf(x, -y + 0.05f, 0.82f, 0.45f);
    glow_of(col, d, cd.accent, 0.4f, 0.55f);
    float a = cover(d);
    if (a <= 0) {
        return;
    }
    // which half of an arm we are on: the sign of cos(5 * angle), without atan
    float px = x;
    float py = -y + 0.05f;
    float r = len(px, py) + 0.0001f;
    float zx = px / r;
    float zy = py / r;
    float x2 = zx * zx - zy * zy;
    float y2 = 2 * zx * zy;
    float x4 = x2 * x2 - y2 * y2;
    float y4 = 2 * x2 * y2;
    float x5 = x4 * zx - y4 * zy;
    float facet = smoothstep(-0.04f, 0.04f, -x5);
    float lit = -0.55f * zx + 0.8f * zy;
    rgb_t bright = {1.0f, 0.95f, 0.6f};
    rgb_t dark = {0.95f, 0.6f, 0.1f};
    rgb_t c = mix(dark, bright, sat(0.35f + 0.45f * facet + 0.25f * lit));
    c = rounded(c, d, 0.06f, 0.2f);
    add_color(&c, WHITE, 0.35f / (1 + r * r * 40));
    paint(col, c, a);
    float band = (x + y) * 0.7f - cd.shine;
    light(col, WHITE, 0.3f * a / (1 + band * band * 50));
}

// --- Heart: beats lub-dub, sending out pulse rings ---

static void draw_heart(rgb_t *col, float x, float y) {
    rgb_t pink = {1.0f, 0.4f, 0.6f};
    if (cd.ring > 0 && cd.ring < 1) {
        float k = 1 + 0.9f * cd.ring;
        float d = fabsf(heart(x / k, (y + 0.05f) / k, 0.62f) * k) - 0.02f;
        light(col, pink, cover(d) * (1 - cd.ring) * 0.8f);
    }
    float k = 1 + 0.12f * cd.pulse;
    float hx = x / (k * (1 + 0.03f * cd.pulse));
    float hy = (y + 0.05f) / k;
    float d = heart(hx, hy, 0.62f);
    glow_of(col, d, pink, 0.35f + 0.3f * cd.pulse, 0.55f);
    float a = cover(d);
    if (a <= 0) {
        return;
    }
    rgb_t c = vgrad(rgb(1, 0.52f, 0.68f), rgb(0.85f, 0.08f, 0.32f), hy, -0.5f, 0.75f);
    c = rounded(c, d, 0.14f, 0.3f);
    paint(col, c, a);
    gloss(col, hx, hy, -0.3f, -0.26f, 0.16f, 0.1f, 0.7f, a);
    gloss(col, hx, hy, 0.32f, -0.3f, 0.07f, 0.05f, 0.4f, a);
}

// --- Initials and avatar ---

static void draw_initials(rgb_t *col, float x, float y, float px, float py) {
    rgb_t top = hsv(initials_hue, 0.5f, 1.0f);
    rgb_t bottom = hsv(initials_hue + 0.08f, 0.85f, 0.62f);
    float r = len(x, y);
    // a soft ring with a bright comet running around it
    float ring = fabsf(r - 0.76f) - 0.01f;
    if (ring < 0.06f) {
        float angle = atan2f(y, x);
        float behind = fmodf(cd.t * 2.2f - angle + 4 * PI, 2 * PI);
        float tail = sat(1 - behind / 2.5f);
        light(col, mix(top, WHITE, 0.4f), cover(ring) * (0.45f + 0.55f * tail * tail));
        light(col, top, 0.4f * tail * tail * halo(maxf(ring, 0), 0.02f));
    }
    if (draw_disc(col, x, y, 0.62f, top, bottom, 0.4f) <= 0) {
        return;
    }
    float cx = HALF + cd.ox * HALF;
    float cy = HALF + (CARD_Y + cd.oy) * HALF;
    float s = maxf(cd.text_pop, 0.01f);
    float tx = cx + (px - cx) / s;
    float ty = cy + (py - cy) / s;
    float shadow = text_alpha(&initials_text, tx, ty - 3);
    paint(col, scaled(bottom, 0.5f), shadow * 0.5f);
    paint(col, WHITE, text_alpha(&initials_text, tx, ty));
}

static float avatar_channel(uint16_t p, int shift, int bits) {
    int max = (1 << bits) - 1;
    return ((p >> shift) & max) / (float)max;
}

static rgb_t avatar_pixel(int x, int y) {
    x = (int)clampf(x, 0, AVATAR_SIZE - 1);
    y = (int)clampf(y, 0, AVATAR_SIZE - 1);
    uint16_t p = avatar[y * AVATAR_SIZE + x];
    return rgb(avatar_channel(p, 11, 5), avatar_channel(p, 5, 6), avatar_channel(p, 0, 5));
}

static rgb_t avatar_sample(float gx, float gy) {
    int ix = (int)floorf(gx);
    int iy = (int)floorf(gy);
    float fx = gx - ix;
    float fy = gy - iy;
    rgb_t top = mix(avatar_pixel(ix, iy), avatar_pixel(ix + 1, iy), fx);
    rgb_t bottom = mix(avatar_pixel(ix, iy + 1), avatar_pixel(ix + 1, iy + 1), fx);
    return mix(top, bottom, fy);
}

static void draw_avatar(rgb_t *col, float x, float y) {
    const float r = 0.62f;
    float d = len(x, y) - r;
    float ring = fabsf(d - 0.07f) - 0.035f;
    glow_of(col, ring, cd.accent, 0.5f, 0.5f);
    if (ring < cd.aa) {
        float angle = atan2f(y, x);
        float wave = 0.5f + 0.5f * cosf(angle - cd.t * 2.5f);
        rgb_t c = mix(cd.accent, WHITE, 0.3f + 0.6f * wave * wave);
        paint(col, c, cover(ring));
    }
    float a = cover(d);
    if (a <= 0) {
        return;
    }
    rgb_t c = avatar_sample((x / r + 1) * AVATAR_SIZE / 2 - 0.5f, (y / r + 1) * AVATAR_SIZE / 2 - 0.5f);
    c = rounded(c, d, 0.15f, 0.35f);
    paint(col, c, a);
    gloss(col, x, y, -0.1f, -0.4f, 0.5f, 0.25f, 0.2f, a);
}

// --- Per-frame animation ---

static void add_ring(float radius, float width, rgb_t color, float alpha) {
    float x = screen_x(0);
    float y = screen_y(0);
    sprite_t *s = sprite_new(SP_RING, x, y, radius + width * 4 + 2 * PX);
    if (s != NULL) {
        s->a = radius;
        s->b = width;
        s->color = color;
        s->alpha = alpha * cd.fade;
    }
}

// Dots that fly out from the middle, slowing down, when something pops in.
static void add_burst(float start, rgb_t color) {
    float t = cd.t - start;
    if (t < 0 || t > 0.8f) {
        return;
    }
    float e = 1 - expf(-5 * t);
    for (int k = 0; k < 12; k++) {
        float angle = k * PI / 6 + 0.2f;
        float dist = (0.3f + 0.25f * hash01(k + 40)) * e + 0.24f;
        float x = screen_x(0) + cosf(angle) * dist;
        float y = screen_y(0) + sinf(angle) * dist;
        sprite_t *s = sprite_new(SP_DOT, x, y, 0.03f);
        if (s != NULL) {
            s->a = 0.012f * (1 - t / 0.8f) + PX;
            s->b = 0.4f;
            s->add = 1;
            s->color = color;
            s->alpha = (1 - t / 0.8f) * cd.fade;
        }
    }
}

// Colored motes drifting up behind every card, for depth.
static void add_motes(void) {
    for (int k = 0; k < 10; k++) {
        float x = lerp(-0.85f, 0.85f, hash01(k + 600)) + 0.03f * sinf(cd.t * 0.8f + k);
        float y = wrap(lerp(-1, 1, hash01(k + 610)) - cd.t * (0.05f + 0.05f * hash01(k + 620)), 2.0f);
        float size = 0.015f + 0.03f * hash01(k + 630);
        sprite_t *s = sprite_new(SP_DOT, x, y, size + PX);
        if (s != NULL) {
            s->a = size;
            s->b = 0.2f;
            s->add = 1;
            s->color = cd.accent;
            s->alpha = 0.12f * cd.fade * (0.6f + 0.4f * sinf(cd.t * 1.5f + k));
        }
    }
}

static void animate_icon(void) {
    float t = cd.t;
    switch (cd.icon) {
    case ICON_MAIL:
        cd.flap = PI * smoothstep(0.35f, 0.8f, t);
        cd.letter = spring(t - 0.75f, 11, 5) + 0.03f * sinf(t * 3) * smoothstep(1.2f, 1.6f, t);
        cd.oy = 0.012f * sinf(t * 2);
        break;
    case ICON_CALENDAR: {
        float since = fmodf(t + 2.4f, 3.0f);
        cd.flip = PI * smoothstep(0, 0.65f, since);
        if (t < 0.6f) {
            cd.flip = 0;
        }
        cd.pulse = 0.5f + 0.5f * sinf(t * 4);
        break;
    }
    case ICON_CHECK:
        cd.draw = ease_out((t - 0.25f) / 0.4f);
        cd.shine = -1.5f + 3 * fmodf(t * 0.45f, 1.0f) * 1.6f;
        add_ring(0.25f + 0.5f * ease_out((t - 0.12f) / 0.6f), 1.5f * PX, cd.accent, 1 - sat((t - 0.12f) / 0.6f));
        add_burst(0.15f, rgb(0.7f, 1, 0.75f));
        break;
    case ICON_CROSS: {
        cd.draw = ease_out((t - 0.25f) / 0.45f);
        float since = fmodf(t - 0.7f, 2.6f);
        cd.shake = 0;
        if (t > 0.7f) {
            cd.shake = 0.08f * sinf(since * 38) * expf(-since * 6);
        }
        cd.shine = 10;
        add_ring(0.25f + 0.5f * ease_out((t - 0.12f) / 0.6f), 1.5f * PX, cd.accent, 1 - sat((t - 0.12f) / 0.6f));
        break;
    }
    case ICON_WARNING: {
        // falls in from above and squashes on landing
        float fall = sat(t / 0.32f);
        cd.oy = -1.0f * (1 - fall * fall);
        float land = t - 0.32f;
        if (land > 0) {
            float squash = 0.25f * expf(-land * 7) * cosf(land * 18);
            cd.sx = 1 + squash;
            cd.sy = 1 - squash;
            cd.oy += squash * 0.1f;
        }
        cd.pulse = 0.5f + 0.5f * sinf(t * 6);
        cd.ring = fmodf(t - 0.4f, 1.3f) / 1.0f;
        if (t < 0.4f) {
            cd.ring = 0;
        }
        cd.bang = 0.12f * sinf(t * 14) * (0.5f + 0.5f * sinf(t * 2));
        break;
    }
    case ICON_CLOCK: {
        float settle = spring(t, 7, 3.5f);
        cd.hand_m = 2 * PI * 0.17f - (1 - settle) * 4 * PI;
        cd.hand_h = 2 * PI * 0.32f - (1 - settle) * PI / 3;
        float tick = floorf(t);
        cd.hand_s = 2 * PI / 60 * (tick + spring(t - tick, 30, 12)) + 2 * PI * 0.6f;
        float since = fmodf(t - 0.9f, 2.0f);
        cd.swing = 0;
        if (t > 0.9f && since < 0.5f) {
            cd.swing = 0.07f * sinf(since * 70) * sinf(PI * since / 0.5f);
        }
        break;
    }
    case ICON_BELL: {
        float since = fmodf(t - 0.25f, 2.4f);
        float amp = 0;
        if (t > 0.25f) {
            amp = 0.5f * expf(-since * 1.8f);
        }
        cd.swing = amp * sinf(since * 2 * PI * 1.7f);
        cd.clapper = 1.3f * amp * sinf((since - 0.08f) * 2 * PI * 1.7f);
        break;
    }
    case ICON_CHAT:
        for (int k = 0; k < 3; k++) {
            float b = maxf(0, sinf(t * 2 * PI * 1.3f - k * 0.9f));
            cd.dots[k] = b * b * smoothstep(0.35f, 0.6f, t);
        }
        cd.sx = 1 + 0.02f * sinf(t * 2.5f);
        cd.sy = 1 - 0.02f * sinf(t * 2.5f);
        break;
    case ICON_CODE: {
        float run = fmodf(maxf(t - 0.45f, 0), 5.0f);
        cd.typed = run * 14;
        cd.cursor = 1;
        if (cd.typed >= 33 && fmodf(t, 0.8f) > 0.45f) {
            cd.cursor = 0;
        }
        break;
    }
    case ICON_MUSIC: {
        const float period = 0.55f;
        cd.beat = fmodf(t, period) / period;
        int n = (int)floorf(t / period);
        float since = t - n * period;
        add_ring(0.3f + 0.35f * since / period, 1.2f * PX, cd.accent, 0.4f * (1 - since / period));
        for (int k = n - 4; k <= n; k++) {
            if (k < 1) {
                continue;
            }
            float age = (t - k * period) / 2.0f;
            if (age < 0 || age > 1) {
                continue;
            }
            float side = -1;
            if (k % 2 == 1) {
                side = 1;
            }
            float x = screen_x(0) + side * (0.18f + 0.3f * age) + 0.04f * sinf(age * 9 + k);
            float y = screen_y(-0.1f) - 0.5f * age;
            float size = 0.05f + 0.015f * hash01(k);
            sprite_t *s = sprite_new(SP_NOTE, x, y, size * 1.4f);
            if (s != NULL) {
                rgb_t colors[4] = {{1, 0.5f, 0.8f}, {0.5f, 0.85f, 1}, {1, 0.85f, 0.4f}, {0.75f, 0.6f, 1}};
                s->a = size;
                s->color = colors[k % 4];
                s->alpha = cd.fade * smoothstep(0, 0.1f, age) * (1 - smoothstep(0.6f, 1, age));
                sprite_rotate(s, 0.4f * sinf(age * 5 + k));
            }
        }
        break;
    }
    case ICON_STAR: {
        cd.spin = -2.4f * (1 - spring(t, 10, 5)) + 0.07f * sinf(t * 2.2f);
        cd.oy = 0.015f * sinf(t * 1.8f);
        cd.shine = -1.5f + 3 * fmodf(t * 0.4f + 0.3f, 1.0f) * 1.6f;
        add_burst(0.1f, rgb(1, 0.9f, 0.5f));
        break;
    }
    case ICON_HEART: {
        float since = fmodf(t - 0.3f, 1.0f);
        float lub = 0;
        if (t > 0.3f && since < 0.14f) {
            lub = sinf(PI * since / 0.14f);
        } else if (since > 0.2f && since < 0.34f) {
            lub = 0.6f * sinf(PI * (since - 0.2f) / 0.14f);
        }
        cd.pulse = lub;
        cd.ring = since / 0.9f;
        if (t < 0.3f) {
            cd.ring = 0;
        }
        for (int k = 0; k < 6; k++) {
            float age = fmodf(t * 0.5f + k / 6.0f, 1.0f);
            if (t < 0.4f + k * 0.15f) {
                continue;
            }
            float x = screen_x(lerp(-0.9f, 0.9f, hash2(k, (int)(t * 0.5f + k / 6.0f)))) + 0.04f * sinf(age * 8 + k);
            float y = screen_y(0.4f) - 0.7f * age;
            float size = 0.03f + 0.02f * hash01(k + 5);
            sprite_t *s = sprite_new(SP_HEART, x, y, size * 1.6f);
            if (s != NULL) {
                s->a = size;
                s->color = rgb(1, 0.55f, 0.72f);
                s->color2 = rgb(0.9f, 0.2f, 0.45f);
                s->alpha = cd.fade * smoothstep(0, 0.15f, age) * (1 - smoothstep(0.6f, 1, age)) * 0.85f;
                sprite_rotate(s, 0.3f * sinf(age * 6 + k));
            }
        }
        break;
    }
    }
}

// A glint blooms on one tip after another, and little sparkles orbit the star.
static void star_glints(void) {
    float t = cd.t;
    float p = fmodf(t, 1.3f) / 1.3f;
    int tip = (int)floorf(t / 1.3f) % 5;
    float angle = -PI / 2 + tip * 2 * PI / 5 - cd.spin;
    float gx = screen_x(cosf(angle) * 0.8f);
    float gy = screen_y(sinf(angle) * 0.8f + 0.05f);
    sprite_t *g = sprite_new(SP_GLINT, gx, gy, 0.15f);
    if (g != NULL && t > 0.6f) {
        g->a = 0.14f * sinf(PI * p);
        g->color = rgb(1, 0.97f, 0.85f);
        g->alpha = cd.fade;
        sprite_rotate(g, t);
    }
    for (int k = 0; k < 3; k++) {
        float a = t * 0.9f + k * 2 * PI / 3;
        sprite_t *s = sprite_new(SP_GLINT, screen_x(cosf(a) * 1.05f), screen_y(sinf(a) * 0.5f - 0.05f), 0.05f);
        if (s != NULL) {
            s->a = 0.035f * (0.6f + 0.4f * sinf(t * 5 + k));
            s->color = cd.accent;
            s->alpha = cd.fade * smoothstep(0.5f, 0.9f, t);
        }
    }
}

void card_set_avatar(const uint16_t *px) {
    memcpy(avatar, px, sizeof avatar);
    float r = 0;
    float g = 0;
    float b = 0;
    for (int i = 0; i < AVATAR_SIZE * AVATAR_SIZE; i += 7) {
        rgb_t c = avatar_pixel(i % AVATAR_SIZE, i / AVATAR_SIZE);
        r += c.r;
        g += c.g;
        b += c.b;
    }
    float most = maxf(maxf(r, g), maxf(b, 0.001f));
    avatar_mean = rgb(0.35f + 0.65f * r / most, 0.35f + 0.65f * g / most, 0.35f + 0.65f * b / most);
}

void card_set_initials(const char *initials) {
    char upper[4];
    int n = 0;
    uint32_t h = 7;
    while (initials && initials[n] && n < 3) {
        char c = initials[n];
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
        upper[n] = c;
        h = h * 31 + (uint8_t)c;
        n++;
    }
    upper[n] = 0;
    initials_hue = hash01(h);
    float cx = HALF;
    float cy = HALF + CARD_Y * HALF;
    text_layout(&initials_text, FONT_BIG, upper, cx, cy, 0.62f * ICON_SCALE * HALF * 1.45f);
}

void card_update(int kind, int icon, float t, float left) {
    cd.kind = kind;
    cd.icon = icon;
    cd.t = t;
    cd.left = left;
    cd.fade = minf(smoothstep(0, 0.12f, t), smoothstep(0, 0.3f, left));
    // pop in with an overshoot; on the way out a small swell, then shrink away
    float pop = 1 - expf(-7 * t) * cosf(13 * t);
    float swell = 0;
    if (left < 0.45f && left > 0.25f) {
        swell = 0.1f * sinf(PI * (0.45f - left) / 0.2f);
    }
    cd.scale = maxf(pop, 0.01f) * (1 + swell) * maxf(smoothstep(0, 0.25f, left), 0.01f);
    cd.sx = 1;
    cd.sy = 1;
    cd.ox = 0;
    cd.oy = 0;
    cd.reach = REACH;
    cd.text_pop = spring(t - 0.12f, 14, 7);
    cd.ray_c = cosf(t * 0.8f);
    cd.ray_s = sinf(t * 0.8f);
    if (kind == CARD_ICON) {
        cd.accent = ACCENTS[icon];
        if (icon == ICON_WARNING) {
            cd.scale = smoothstep(0, 0.25f, left) + 0.01f;
            cd.reach = 1.75f;
        }
        if (icon == ICON_HEART) {
            cd.reach = 1.75f;
        }
    } else if (kind == CARD_INITIALS) {
        cd.accent = hsv(initials_hue, 0.5f, 1.0f);
    } else {
        cd.accent = avatar_mean;
    }
    sprites_clear();
    add_motes();
    if (kind != CARD_ICON) {
        add_ring(0.26f + 0.5f * ease_out((t - 0.1f) / 0.7f), 1.5f * PX, cd.accent, 0.8f * (1 - sat((t - 0.1f) / 0.7f)));
    }
    cd.swing = 0;
    cd.clapper = 0;
    cd.shake = 0;
    cd.spin = 0;
    cd.bang = 0;
    if (kind == CARD_ICON) {
        animate_icon();
    }
    float turn = cd.swing + cd.shake * 1.5f + cd.spin;
    float part = cd.clapper + cd.bang;
    cd.turn_c = cosf(turn);
    cd.turn_s = sinf(turn);
    cd.part_c = cosf(part);
    cd.part_s = sinf(part);
    cd.split = sprites_count();
    if (kind == CARD_ICON && icon == ICON_STAR) {
        star_glints();
    }
    float smallest = minf(cd.sx, cd.sy) * cd.scale;
    cd.aa = PX / (ICON_SCALE * smallest);
}

void card_draw(rgb_t *row, int j) {
    float v = row_v(j);
    int i0;
    int i1;
    row_span(v, -1, 1, &i0, &i1);
    // the card's color glows through the whole ball, with soft rays turning slowly
    float dy = v - CARD_Y;
    float amb = 0.13f * cd.fade;
    for (int i = i0; i < i1; i++) {
        float u = pixel_u(i);
        float r2 = u * u + dy * dy;
        float r = sqrtf(r2) + 0.0001f;
        float zx = u / r;
        float zy = dy / r;
        float x2 = zx * zx - zy * zy;
        float y2 = 2 * zx * zy;
        float x4 = x2 * x2 - y2 * y2;
        float y4 = 2 * x2 * y2;
        float x8 = x4 * x4 - y4 * y4;
        float y8 = 2 * x4 * y4;
        float ray = sat(x8 * cd.ray_c - y8 * cd.ray_s);
        float k = amb / (1 + r2 * 4) + cd.fade * 0.05f * ray * ray / (1 + r2 * 2);
        add_color(&row[i], cd.accent, k);
    }
    sprites_draw(row, j, 0, cd.split);

    float unit_x = ICON_SCALE * cd.scale * cd.sx;
    float unit_y = ICON_SCALE * cd.scale * cd.sy;
    float cy = CARD_Y + cd.oy;
    float half_h = cd.reach * unit_y;
    if (v > cy - half_h && v < cy + half_h && cd.fade > 0) {
        float y = (v - cy) / unit_y;
        float py = j + 0.5f;
        row_span(v, cd.ox - cd.reach * unit_x, cd.ox + cd.reach * unit_x, &i0, &i1);
        for (int i = i0; i < i1; i++) {
            float x = (pixel_u(i) - cd.ox) / unit_x;
            if (cd.kind == CARD_INITIALS) {
                draw_initials(&row[i], x, y, i + 0.5f, py);
                continue;
            }
            if (cd.kind == CARD_AVATAR) {
                draw_avatar(&row[i], x, y);
                continue;
            }
            switch (cd.icon) {
            case ICON_MAIL:
                draw_mail(&row[i], x, y);
                break;
            case ICON_CALENDAR:
                draw_calendar(&row[i], x, y);
                break;
            case ICON_CHECK:
                draw_check(&row[i], x, y);
                break;
            case ICON_CROSS:
                draw_cross(&row[i], x, y);
                break;
            case ICON_WARNING:
                draw_warning(&row[i], x, y);
                break;
            case ICON_CLOCK:
                draw_clock(&row[i], x, y);
                break;
            case ICON_BELL:
                draw_bell(&row[i], x, y);
                break;
            case ICON_CHAT:
                draw_chat(&row[i], x, y);
                break;
            case ICON_CODE:
                draw_code(&row[i], x, y);
                break;
            case ICON_MUSIC:
                draw_music(&row[i], x, y);
                break;
            case ICON_STAR:
                draw_star(&row[i], x, y);
                break;
            case ICON_HEART:
                draw_heart(&row[i], x, y);
                break;
            }
        }
    }
    sprites_draw(row, j, cd.split, sprites_count());
}
