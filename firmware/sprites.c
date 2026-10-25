#include "sprites.h"
#include <string.h>

typedef struct {
    int count;
    float x[CLOUD_PUFFS], y[CLOUD_PUFFS], r[CLOUD_PUFFS];
} cloud_t;

static sprite_t sprites[MAX_SPRITES];
static int sprite_count;
static cloud_t clouds[MAX_CLOUDS];
static int cloud_count;

static const rgb_t WHITE = {1, 1, 1};

void sprites_clear(void) {
    sprite_count = 0;
    cloud_count = 0;
}

int sprites_count(void) {
    return sprite_count;
}

sprite_t *sprite_new(int kind, float x, float y, float reach) {
    if (sprite_count >= MAX_SPRITES) {
        return NULL;
    }
    sprite_t *s = &sprites[sprite_count];
    sprite_count++;
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->x = x;
    s->y = y;
    s->ax = 1;
    s->alpha = 1;
    s->color = WHITE;
    s->color2 = WHITE;
    s->x0 = x - reach;
    s->x1 = x + reach;
    s->y0 = y - reach;
    s->y1 = y + reach;
    if (kind == SP_STREAK || kind == SP_GLINT || kind == SP_RING || kind == SP_BOLT) {
        s->add = 1;
    }
    return s;
}

sprite_t *sprite_cloud(void) {
    if (cloud_count >= MAX_CLOUDS) {
        return NULL;
    }
    sprite_t *s = sprite_new(SP_CLOUD, 0, 0, 0);
    if (s == NULL) {
        return NULL;
    }
    s->b = (float)cloud_count;
    clouds[cloud_count].count = 0;
    cloud_count++;
    s->x0 = 10;
    s->x1 = -10;
    s->y0 = 10;
    s->y1 = -10;
    return s;
}

void cloud_puff(sprite_t *s, float x, float y, float r) {
    cloud_t *c = &clouds[(int)s->b];
    if (c->count >= CLOUD_PUFFS) {
        return;
    }
    c->x[c->count] = x;
    c->y[c->count] = y;
    c->r[c->count] = r;
    c->count++;
    float reach = r + 2 * s->a;
    s->x0 = minf(s->x0, x - reach);
    s->x1 = maxf(s->x1, x + reach);
    s->y0 = minf(s->y0, y - reach);
    s->y1 = maxf(s->y1, y + reach);
    // the center, used for the rim light, is the middle of the bounds
    s->x = (s->x0 + s->x1) / 2;
    s->y = (s->y0 + s->y1) / 2;
}

void sprite_rotate(sprite_t *s, float angle) {
    s->ax = cosf(angle);
    s->ay = sinf(angle);
}

static void paint(rgb_t *col, const sprite_t *s, rgb_t c, float k) {
    if (s->add) {
        add_color(col, c, k);
    } else {
        over(col, c, k);
    }
}

// --- One function per kind, each drawing pixels i0..i1-1 of a row ---

// Smooth minimum: blends two distances so the puffs melt into one shape.
static float smin(float a, float b, float k) {
    float h = maxf(k - fabsf(a - b), 0) / k;
    return minf(a, b) - h * h * k * 0.25f;
}

static void draw_cloud(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    const cloud_t *c = &clouds[(int)s->b];
    float height = sat((s->d - v) / s->e);  // 0 at the base, 1 at the top
    float blend = s->e * 0.12f;
    float soft = s->a * (2 - height);       // softer underneath
    float inv_w = 2 / (s->x1 - s->x0);
    float inv_h = 2 / (s->y1 - s->y0);
    float ny = (v - s->y) * inv_h;
    // only puffs that reach this row matter
    float px[CLOUD_PUFFS];
    float pdy[CLOUD_PUFFS];
    float dy2[CLOUD_PUFFS];
    float pr[CLOUD_PUFFS];
    float reach2[CLOUD_PUFFS];
    float left = 10;
    float right = -10;
    int n = 0;
    for (int k = 0; k < c->count; k++) {
        float dy = v - c->y[k];
        float reach = c->r[k] + blend + soft;
        if (fabsf(dy) < reach) {
            px[n] = c->x[k];
            pdy[n] = dy;
            dy2[n] = dy * dy;
            pr[n] = c->r[k];
            reach2[n] = reach * reach;
            float half = sqrtf(reach * reach - dy * dy);
            left = minf(left, c->x[k] - half);
            right = maxf(right, c->x[k] + half);
            n++;
        }
    }
    if (n == 0) {
        return;
    }
    row_span(v, maxf(left, s->x0), minf(right, s->x1), &i0, &i1);
    for (int i = i0; i < i1; i++) {
        float u = pixel_u(i);
        float d = 10;
        float dome = -1;
        for (int k = 0; k < n; k++) {
            float dx = u - px[k];
            float dist2 = dx * dx + dy2[k];
            if (dist2 > reach2[k]) {
                continue;
            }
            float dist = sqrtf(dist2);
            d = smin(d, dist - pr[k], blend);
            // light on this puff as if it were a ball lit from above; the brightest one wins
            float nz = sqrtf(sat(1 - dist2 / (pr[k] * pr[k])));
            dome = maxf(dome, 0.7f * nz - 0.7f * pdy[k] / pr[k]);
        }
        float cover = fill(d, soft);
        if (cover <= 0) {
            continue;
        }
        float depth = sat(-d / (s->e * 0.5f));
        float nx = (u - s->x) * inv_w;
        float facing = sat(nx * s->ax + ny * s->ay);
        // each puff is a lit dome inside the merged shape
        float lit = sat(0.1f + 0.55f * height + 0.3f * dome);
        rgb_t col = mix(s->color2, s->color, lit);
        float edge = 1 - depth;
        add_color(&col, s->color, s->c * edge * edge * edge * facing);
        over(&row[i], col, cover * s->alpha);
    }
}

static void draw_streak(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float py = v - s->y;
    float d2 = s->ax * s->ax + s->ay * s->ay;
    if (d2 < 0.000001f) {
        return;
    }
    float soft = s->a + PX;
    for (int i = i0; i < i1; i++) {
        float px = pixel_u(i) - s->x;
        float h = clampf((px * s->ax + py * s->ay) / d2, 0, 1);
        float dist = len(px - s->ax * h, py - s->ay * h);
        float cover = 1 - dist / soft;
        if (cover <= 0) {
            continue;
        }
        add_color(&row[i], s->color, cover * h * h * s->alpha);
    }
}

static void draw_dot(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float dy = v - s->y;
    float r = maxf(s->a, 0.7f * PX);
    float inner = r * s->b;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float dist = len(dx, dy);
        if (dist >= r + PX) {
            continue;
        }
        float cover;
        if (s->b >= 0.99f) {
            cover = fill(dist - r, PX);
        } else {
            cover = smoothstep(r, inner, dist);
        }
        paint(&row[i], s, s->color, cover * s->alpha);
    }
}

// Distance to one arm of a snowflake lying along +x, with two side branches.
static float arm_distance(float along, float across, float size) {
    float s = fabsf(along);
    float t = fabsf(across);
    float d = segment(s, t, 0, 0, size, 0, 0);
    float b = segment(s, t, size * 0.5f, 0, size * 0.78f, size * 0.26f, 0);
    return minf(d, b);
}

static void draw_crystal(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    const float C60 = 0.5f;
    const float S60 = 0.8660254f;
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float lx = dx * s->ax + dy * s->ay;
        float ly = -dx * s->ay + dy * s->ax;
        float d = arm_distance(lx, ly, s->a);
        d = minf(d, arm_distance(lx * C60 + ly * S60, -lx * S60 + ly * C60, s->a));
        d = minf(d, arm_distance(lx * C60 - ly * S60, lx * S60 + ly * C60, s->a));
        d -= s->b;
        float core = fill(d, PX);
        float glow = 0.35f * halo(maxf(d, 0), s->a * 0.35f);
        over(&row[i], s->color, core * s->alpha);
        add_color(&row[i], s->color, glow * s->alpha * 0.4f);
    }
}

static void draw_ripple(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float dy = (v - s->y) / s->b;
    float front = 0.6f;
    if (dy > 0) {
        front = 1.0f;
    }
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float d = fabsf(len(dx, dy) - s->a) - s->c;
        float cover = fill(d * s->b * 1.6f, PX);
        if (cover <= 0) {
            continue;
        }
        add_color(&row[i], s->color, cover * front * s->alpha);
    }
}

static void draw_bead(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float r = s->a;
    if (r < PX) {
        return;
    }
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float dist = len(dx, dy);
        // shadow on the glass below and to the right
        float sd = len(dx - r * 0.25f, dy - r * 0.35f) - r;
        float shadow = fill(sd, r * 0.5f) * 0.35f;
        if (dist > r) {
            over(&row[i], rgb(0, 0, 0), shadow * s->alpha);
        }
        float cover = fill(dist - r, PX);
        if (cover <= 0) {
            continue;
        }
        // the drop bends light: dark rim, bright bottom, small highlight at the top
        float edge = smoothstep(0.5f * r, r, dist);
        float bottom = sat(dy / r);
        rgb_t c = mix(row[i], s->color, 0.35f + 0.4f * bottom * bottom);
        c = mix(c, scaled(row[i], 0.45f), edge * 0.7f);
        float spot = len(dx + r * 0.35f, dy + r * 0.4f);
        float shine = sat(1 - spot / (r * 0.32f));
        add_color(&c, WHITE, shine * shine * 0.9f);
        over(&row[i], c, cover * s->alpha);
    }
}

static void draw_bolt(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float py = v - s->y;
    float d2 = s->ax * s->ax + s->ay * s->ay;
    for (int i = i0; i < i1; i++) {
        float px = pixel_u(i) - s->x;
        float h = clampf((px * s->ax + py * s->ay) / d2, 0, 1);
        float d = len(px - s->ax * h, py - s->ay * h) - s->a;
        float core = fill(d, PX);
        float fade = sat(1 - d / (5 * s->b));
        float glow = halo(maxf(d, 0), s->b) * fade * fade;
        add_color(&row[i], s->color2, glow * 0.6f * s->alpha);
        add_color(&row[i], s->color, core * s->alpha);
    }
}

static void draw_paper(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float lx = dx * s->ax + dy * s->ay;
        float ly = -dx * s->ay + dy * s->ax;
        float qx = fabsf(lx) - s->a;
        float qy = fabsf(ly) - s->b;
        float d = maxf(qx, qy);
        float cover = fill(d, 0.7f * PX);
        if (cover <= 0) {
            continue;
        }
        paint(&row[i], s, s->color, cover * s->alpha);
    }
}

static void draw_sequin(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float dy = v - s->y;
    float small = minf(s->a, s->b);
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float lx = (dx * s->ax + dy * s->ay) / s->a;
        float ly = (-dx * s->ay + dy * s->ax) / s->b;
        float k = len(lx, ly);
        float cover = fill((k - 1) * small, 0.7f * PX);
        if (cover <= 0) {
            continue;
        }
        rgb_t c = s->color;
        float shine = sat(1 - len(lx + 0.35f, ly + 0.35f) * 1.8f);
        add_color(&c, WHITE, shine * 0.7f);
        paint(&row[i], s, c, cover * s->alpha);
    }
}

static void draw_glint(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float dy = v - s->y;
    float inv = 1 / s->a;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float lx = fabsf(dx * s->ax + dy * s->ay) * inv;
        float ly = fabsf(-dx * s->ay + dy * s->ax) * inv;
        float arm1 = sat(1 - lx - ly * 7);
        float arm2 = sat(1 - ly - lx * 7);
        float p = (lx + ly) * 0.7071f;
        float q = fabsf(lx - ly) * 0.7071f;
        float arm3 = sat(1 - p * 2.2f - q * 9) * 0.6f;
        float r2 = lx * lx + ly * ly;
        if (r2 >= 1) {
            continue;
        }
        float edge = (1 - r2) * (1 - r2);
        float core = 1 / (1 + r2 * 120);
        float glow = 0.35f / (1 + r2 * 12);
        float lit = arm1 * arm1 + arm2 * arm2 + arm3 * arm3 + (core + glow) * edge;
        add_color(&row[i], s->color, lit * s->alpha);
    }
}

static void draw_zed(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float a = s->a;
    float w = s->b;
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float lx = dx * s->ax + dy * s->ay;
        float ly = -dx * s->ay + dy * s->ax;
        float d = segment(lx, ly, -a, -a, a, -a, w);
        d = minf(d, segment(lx, ly, a, -a, -a, a, w));
        d = minf(d, segment(lx, ly, -a, a, a, a, w));
        float glow = sat(1 - d / (a * 1.2f));
        add_color(&row[i], s->color, (fill(d, PX) + 0.35f * glow * glow) * s->alpha);
    }
}

static void draw_ring(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float d = fabsf(len(dx, dy) - s->a) - s->b;
        float lit = fill(d, PX) + 0.4f * halo(maxf(d, 0), s->b * 3 + PX);
        add_color(&row[i], s->color, lit * s->alpha);
    }
}

static void draw_heart(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float lx = dx * s->ax + dy * s->ay;
        float ly = -dx * s->ay + dy * s->ax;
        float d = heart(lx, ly, s->a);
        float cover = fill(d, PX);
        rgb_t c = mix(s->color, s->color2, sat(0.5f + ly / (2 * s->a)));
        float shine = sat(1 - len(lx + s->a * 0.4f, ly + s->a * 0.35f) / (s->a * 0.35f));
        add_color(&c, WHITE, shine * 0.6f);
        over(&row[i], c, cover * s->alpha);
        add_color(&row[i], s->color, 0.25f * halo(maxf(d, 0), s->a * 0.5f) * s->alpha);
    }
}

static void draw_note(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float a = s->a;
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float lx = dx * s->ax + dy * s->ay;
        float ly = -dx * s->ay + dy * s->ax;
        // tilted oval head
        float hx = lx + 0.3f * a;
        float hy = ly - 0.5f * a;
        float rx = (hx * 0.94f + hy * 0.34f) / (0.36f * a);
        float ry = (-hx * 0.34f + hy * 0.94f) / (0.26f * a);
        float d = (len(rx, ry) - 1) * 0.26f * a;
        d = minf(d, segment(lx, ly, 0.03f * a, 0.45f * a, 0.03f * a, -0.75f * a, 0.07f * a));
        d = minf(d, segment(lx, ly, 0.03f * a, -0.75f * a, 0.42f * a, -0.3f * a, 0.08f * a));
        float cover = fill(d, PX);
        over(&row[i], s->color, cover * s->alpha);
        add_color(&row[i], s->color, 0.3f * halo(maxf(d, 0), a * 0.3f) * s->alpha);
    }
}

static void draw_moon(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float r = s->a;
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float dist = len(dx, dy);
        float bite = len(dx - r * 0.45f, dy + r * 0.3f) - r * 0.85f;
        float d = maxf(dist - r, -bite);
        float glow = sat(1 - dist / (3 * r));
        add_color(&row[i], s->color2, glow * glow * 0.35f * s->alpha);
        rgb_t c = mix(s->color, scaled(s->color, 0.75f), sat((dx + dy) / r * 0.5f + 0.5f));
        over(&row[i], c, fill(d, PX) * s->alpha);
    }
}

static void draw_bird(rgb_t *row, const sprite_t *s, float v, int i0, int i1) {
    float a = s->a;
    float tip = -s->b * a * 0.6f;
    float dy = v - s->y;
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - s->x;
        float d = segment(fabsf(dx), dy, 0, 0, a * 0.5f, tip * 0.6f - a * 0.12f, 0);
        d = minf(d, segment(fabsf(dx), dy, a * 0.5f, tip * 0.6f - a * 0.12f, a, tip, 0));
        float cover = fill(d - 0.6f * PX, PX);
        over(&row[i], s->color, cover * s->alpha);
    }
}

void sprites_draw(rgb_t *row, int j, int first, int last) {
    float v = row_v(j);
    if (last > sprite_count) {
        last = sprite_count;
    }
    for (int k = first; k < last; k++) {
        const sprite_t *s = &sprites[k];
        if (v < s->y0 || v > s->y1 || s->alpha <= 0) {
            continue;
        }
        int i0;
        int i1;
        row_span(v, s->x0, s->x1, &i0, &i1);
        if (i0 >= i1) {
            continue;
        }
        switch (s->kind) {
        case SP_CLOUD:
            draw_cloud(row, s, v, i0, i1);
            break;
        case SP_STREAK:
            draw_streak(row, s, v, i0, i1);
            break;
        case SP_DOT:
            draw_dot(row, s, v, i0, i1);
            break;
        case SP_CRYSTAL:
            draw_crystal(row, s, v, i0, i1);
            break;
        case SP_RIPPLE:
            draw_ripple(row, s, v, i0, i1);
            break;
        case SP_BEAD:
            draw_bead(row, s, v, i0, i1);
            break;
        case SP_BOLT:
            draw_bolt(row, s, v, i0, i1);
            break;
        case SP_PAPER:
            draw_paper(row, s, v, i0, i1);
            break;
        case SP_SEQUIN:
            draw_sequin(row, s, v, i0, i1);
            break;
        case SP_GLINT:
            draw_glint(row, s, v, i0, i1);
            break;
        case SP_ZED:
            draw_zed(row, s, v, i0, i1);
            break;
        case SP_RING:
            draw_ring(row, s, v, i0, i1);
            break;
        case SP_HEART:
            draw_heart(row, s, v, i0, i1);
            break;
        case SP_NOTE:
            draw_note(row, s, v, i0, i1);
            break;
        case SP_MOON:
            draw_moon(row, s, v, i0, i1);
            break;
        case SP_BIRD:
            draw_bird(row, s, v, i0, i1);
            break;
        }
    }
}
