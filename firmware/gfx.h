// Small math, color and shape helpers shared by the face, scenes and cards.
#pragma once
#include "eye.h"
#include <math.h>
#include <stdint.h>

#define HALF (EYE_SIZE / 2)
#define PX (1.0f / HALF)  // one pixel in screen units (screen radius 1)
#define PI 3.14159265f

typedef struct {
    float r, g, b;
} rgb_t;

// --- Math ---

static inline float clampf(float x, float lo, float hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

static inline float sat(float x) {
    return clampf(x, 0, 1);
}

static inline float smoothstep(float a, float b, float x) {
    x = clampf((x - a) / (b - a), 0, 1);
    return x * x * (3 - 2 * x);
}

static inline float minf(float a, float b) {
    return a < b ? a : b;
}

static inline float maxf(float a, float b) {
    return a > b ? a : b;
}

static inline float lerp(float a, float b, float k) {
    return a + (b - a) * k;
}

static inline float len(float x, float y) {
    return sqrtf(x * x + y * y);
}

// Wraps x into -w/2 .. w/2, for things that drift across and come back.
static inline float wrap(float x, float w) {
    x = fmodf(x + w / 2, w);
    if (x < 0) {
        x += w;
    }
    return x - w / 2;
}

// Stable pseudo-random 0..1 for n, so particles need no stored state.
static inline float hash01(uint32_t n) {
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n = n ^ (n >> 4);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15);
    return (n & 0xffffu) / 65535.0f;
}

static inline float hash2(uint32_t a, uint32_t b) {
    return hash01(a * 7919u + b * 104729u + 17u);
}

// --- Easing, all 0..1 in and out ---

static inline float ease_out(float x) {
    x = 1 - sat(x);
    return 1 - x * x * x;
}

static inline float ease_in(float x) {
    x = sat(x);
    return x * x * x;
}

// Overshoots past 1 and settles: a pop.
static inline float ease_back(float x) {
    x = sat(x) - 1;
    return 1 + x * x * (2.7f * x + 1.7f);
}

// Damped spring from 0 to 1 over time t (seconds).
static inline float spring(float t, float freq, float damp) {
    if (t <= 0) {
        return 0;
    }
    return 1 - expf(-damp * t) * cosf(freq * t);
}

// --- Color ---

static inline rgb_t rgb(float r, float g, float b) {
    rgb_t c = {r, g, b};
    return c;
}

static inline rgb_t mix(rgb_t a, rgb_t b, float k) {
    rgb_t c = {lerp(a.r, b.r, k), lerp(a.g, b.g, k), lerp(a.b, b.b, k)};
    return c;
}

static inline rgb_t scaled(rgb_t c, float k) {
    rgb_t out = {c.r * k, c.g * k, c.b * k};
    return out;
}

static inline void add_color(rgb_t *col, rgb_t c, float k) {
    col->r += c.r * k;
    col->g += c.g * k;
    col->b += c.b * k;
}

// Paints c over col with opacity a.
static inline void over(rgb_t *col, rgb_t c, float a) {
    col->r += (c.r - col->r) * a;
    col->g += (c.g - col->g) * a;
    col->b += (c.b - col->b) * a;
}

// Hue 0..1 to a color.
static inline rgb_t hsv(float h, float s, float v) {
    h = (h - floorf(h)) * 6;
    float r = sat(fabsf(h - 3) - 1);
    float g = sat(2 - fabsf(h - 2));
    float b = sat(2 - fabsf(h - 4));
    rgb_t c = {v * lerp(1, r, s), v * lerp(1, g, s), v * lerp(1, b, s)};
    return c;
}

// --- Coverage ---

// Antialiased fill for a signed distance d, with edge width aa.
static inline float fill(float d, float aa) {
    if (aa < 0.0001f) {
        aa = 0.0001f;
    }
    return clampf(0.5f - d / (2 * aa), 0, 1);
}

// Soft glow that is 1 at the edge and fades with distance.
static inline float halo(float d, float reach) {
    if (d <= 0) {
        return 1;
    }
    float k = d / reach;
    return 1 / (1 + k * k);
}

// --- Pixels ---

static inline float pixel_u(int i) {
    return (i + 0.5f) / HALF - 1;
}

static inline float row_v(int j) {
    return (j + 0.5f) / HALF - 1;
}

// Pixel range [i0, i1) covering x0..x1 on row v, clipped to the ball.
static inline void row_span(float v, float x0, float x1, int *i0, int *i1) {
    float chord = sqrtf(maxf(1 - v * v, 0));
    x0 = maxf(x0, -chord);
    x1 = minf(x1, chord);
    *i0 = (int)floorf((x0 + 1) * HALF);
    *i1 = (int)ceilf((x1 + 1) * HALF);
    if (*i0 < 0) {
        *i0 = 0;
    }
    if (*i1 > EYE_SIZE) {
        *i1 = EYE_SIZE;
    }
}

// --- Shapes, as signed distances ---

static inline float pill(float x, float y, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(x - cx) - hw;
    float qy = fabsf(y - cy) - hh;
    return len(maxf(qx, 0), maxf(qy, 0)) + minf(maxf(qx, qy), 0) - r;
}

static inline float rbox(float x, float y, float cx, float cy, float hw, float hh, float r) {
    return pill(x, y, cx, cy, hw - r, hh - r, r);
}

static inline float segment(float x, float y, float ax, float ay, float bx, float by, float w) {
    float px = x - ax;
    float py = y - ay;
    float dx = bx - ax;
    float dy = by - ay;
    float h = clampf((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1);
    return len(px - dx * h, py - dy * h) - w;
}

static inline float ring_sdf(float x, float y, float cx, float cy, float r, float w) {
    return fabsf(len(x - cx, y - cy) - r) - w;
}

// Triangle with corners a, b, c (iq).
static inline float triangle(float x, float y, float ax, float ay, float bx, float by, float cx, float cy) {
    float e0x = bx - ax;
    float e0y = by - ay;
    float e1x = cx - bx;
    float e1y = cy - by;
    float e2x = ax - cx;
    float e2y = ay - cy;
    float v0x = x - ax;
    float v0y = y - ay;
    float v1x = x - bx;
    float v1y = y - by;
    float v2x = x - cx;
    float v2y = y - cy;
    float h0 = clampf((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y), 0, 1);
    float h1 = clampf((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y), 0, 1);
    float h2 = clampf((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y), 0, 1);
    float p0x = v0x - e0x * h0;
    float p0y = v0y - e0y * h0;
    float p1x = v1x - e1x * h1;
    float p1y = v1y - e1y * h1;
    float p2x = v2x - e2x * h2;
    float p2y = v2y - e2y * h2;
    float s = e0x * e2y - e0y * e2x;
    float d0 = p0x * p0x + p0y * p0y;
    float d1 = p1x * p1x + p1y * p1y;
    float d2 = p2x * p2x + p2y * p2y;
    float s0 = s * (v0x * e0y - v0y * e0x);
    float s1 = s * (v1x * e1y - v1y * e1x);
    float s2 = s * (v2x * e2y - v2y * e2x);
    float d = minf(minf(d0, d1), d2);
    float inside = minf(minf(s0, s1), s2);
    if (inside > 0) {
        return -sqrtf(d);
    }
    return sqrtf(d);
}

// Five-point star, y up (iq).
static inline float star_sdf(float x, float y, float r, float rf) {
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

// Heart, y up, spanning about 0..1 (iq).
static inline float heart_sdf(float x, float y) {
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

// Heart centered on 0,0 with y down, about 2*s tall.
static inline float heart(float x, float y, float s) {
    return heart_sdf(x / (1.3f * s), -y / (1.3f * s) + 0.55f) * 1.3f * s;
}

// --- Glass ---

// The ball's top-left sheen and bright rim, added on top of a full-ball scene.
static inline void glass_shine(rgb_t *col, float u, float v, float r2, float k) {
    float su = u + 0.35f;
    float sv = v + 0.45f;
    float s2 = su * su + sv * sv;
    float sheen = 0.1f / (1 + s2 * 9);
    float rim = 0;
    if (r2 > 0.8f) {
        rim = 0.12f * smoothstep(0.8f, 1.0f, r2) * (0.6f - 0.4f * v);
    }
    float edge = 1 - 0.35f * smoothstep(0.55f, 1.0f, r2);  // darker toward the edge, so it reads round
    col->r = (col->r * edge + sheen + rim) * k + col->r * (1 - k);
    col->g = (col->g * edge + sheen + rim) * k + col->g * (1 - k);
    col->b = (col->b * edge + sheen * 1.1f + rim * 1.1f) * k + col->b * (1 - k);
}
