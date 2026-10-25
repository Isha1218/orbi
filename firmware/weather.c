// Rain, storm, snow, sun and cloud. Each paints the whole ball: a sky, layers of sprites
// at different depths, and the ball's glass on top. The scene opens like a round window.
#include "weather.h"
#include "sprites.h"

#define OPEN_TIME 0.7f
#define CLOSE_TIME 0.45f
#define HORIZON 0.36f      // water line for rain and storm
#define RIPPLE_TIME 0.5f

typedef struct {
    float x, y, r;
} puff_shape_t;

// A cloud is a handful of puffs, upper ones first so the lower ones overlap them.
static const puff_shape_t CLOUD_SHAPE[6] = {
    {0.08f, -0.17f, 0.27f},
    {-0.26f, -0.05f, 0.22f},
    {0.38f, -0.02f, 0.21f},
    {-0.52f, 0.08f, 0.16f},
    {0.62f, 0.09f, 0.14f},
    {0.02f, 0.09f, 0.23f},
};

typedef struct {
    rgb_t lit, shade;
    float rim;                // silver lining on the lit side
    float soft;               // edge softness
    float alpha;
    float light_x, light_y;   // the point the puffs are lit from
    float roil;               // how much the puffs churn
} cloud_style_t;

typedef struct {
    int count;
    float speed, length, width, alpha;
    float ground0, ground1;   // where drops land, screen y
    float ripple;             // ripple size when they land, 0 for none
} rain_layer_t;

static struct {
    int scene;
    float t;
    float reveal;              // radius of the open window
    float flash;               // lightning, 0..1
    float flash_x;
    int split;                 // sprites before this draw behind the ground
    float sun_x, sun_y, sun_r;
    float rays;                // how far the sun's rays reach, 0..1
    float ray_c1, ray_s1, ray_c2, ray_s2;
    float bank;                // snow bank height
    float ground[EYE_SIZE];    // per-column top of the snow bank or near hill
    float ground2[EYE_SIZE];   // per-column top of the far hill
} w;

static rgb_t buf[EYE_SIZE];

static const rgb_t WHITE = {1, 1, 1};

// --- Building blocks ---

static void add_cloud(float cx, float cy, float scale, uint32_t seed, const cloud_style_t *st) {
    sprite_t *s = sprite_cloud();
    if (s == NULL) {
        return;
    }
    s->a = st->soft;
    s->c = st->rim;
    s->d = cy + 0.2f * scale;
    s->e = 0.6f * scale;
    s->color = st->lit;
    s->color2 = st->shade;
    s->alpha = st->alpha;
    for (int k = 0; k < 6; k++) {
        const puff_shape_t *p = &CLOUD_SHAPE[k];
        float x = cx + (p->x + (hash2(seed, k) - 0.5f) * 0.12f) * scale;
        float y = cy + (p->y + (hash2(seed, k + 10) - 0.5f) * 0.08f) * scale;
        float r = p->r * scale * (0.85f + 0.3f * hash2(seed, k + 20));
        r *= 1 + st->roil * sinf(w.t * (1.2f + st->roil * 12) + k * 1.7f + seed);
        cloud_puff(s, x, y, r);
    }
    float lx = st->light_x - s->x;
    float ly = st->light_y - s->y;
    float l = len(lx, ly) + 0.0001f;
    s->ax = lx / l;
    s->ay = ly / l;
    if (w.flash > 0) {
        // lightning lights the clouds from inside, most near the strike
        float k = w.flash * halo(fabsf(s->x - w.flash_x), 0.35f);
        add_color(&s->color, rgb(0.6f, 0.58f, 0.85f), k);
        add_color(&s->color2, rgb(0.35f, 0.32f, 0.6f), k);
        s->c += k;
    }
}

// Clouds spread across a row, drifting sideways and wrapping around.
static void add_cloud_row(int count, float y, float scale, float drift, uint32_t seed, const cloud_style_t *st) {
    float span = 2.3f + 1.4f * scale;
    for (int c = 0; c < count; c++) {
        float x = wrap(-1 + span * (c + 0.6f * hash2(seed, c + 40)) / count + drift, span);
        float cy = y + (hash2(seed, c + 30) - 0.5f) * 0.12f;
        float size = scale * (0.8f + 0.4f * hash2(seed, c + 50));
        add_cloud(x, cy, size, seed * 31 + c, st);
    }
}

static sprite_t *add_streak(float x, float y, float dx, float dy, float width, rgb_t color, float alpha) {
    sprite_t *s = sprite_new(SP_STREAK, x, y, 0);
    if (s == NULL) {
        return NULL;
    }
    s->ax = dx;
    s->ay = dy;
    s->a = width;
    s->color = color;
    s->alpha = alpha;
    s->x0 = minf(x, x + dx) - width - 2 * PX;
    s->x1 = maxf(x, x + dx) + width + 2 * PX;
    s->y0 = minf(y, y + dy) - width - 2 * PX;
    s->y1 = maxf(y, y + dy) + width + 2 * PX;
    return s;
}

static void add_ripple(float x, float y, float r, float alpha, rgb_t color) {
    const float flat = 0.28f;
    sprite_t *s = sprite_new(SP_RIPPLE, x, y, 0);
    if (s == NULL) {
        return;
    }
    s->a = r;
    s->b = flat;
    s->c = 0.7f * PX / flat;
    s->color = color;
    s->alpha = alpha;
    s->x0 = x - r - 0.02f;
    s->x1 = x + r + 0.02f;
    s->y0 = y - r * flat - 3 * PX;
    s->y1 = y + r * flat + 3 * PX;
}

static void add_dot(float x, float y, float r, float hardness, rgb_t color, float alpha, int add) {
    sprite_t *s = sprite_new(SP_DOT, x, y, r + 2 * PX);
    if (s == NULL) {
        return;
    }
    s->a = r;
    s->b = hardness;
    s->color = color;
    s->alpha = alpha;
    s->add = add;
}

// Drops fall from the clouds at top, then leave a ripple (and a splash) where they land.
static void add_rain(const rain_layer_t *layer, uint32_t id_base, float slant, float top, rgb_t color) {
    for (int i = 0; i < layer->count; i++) {
        uint32_t id = id_base + i;
        float ground = lerp(layer->ground0, layer->ground1, hash01(id * 3 + 1));
        float fall = (ground - top) / layer->speed;
        float life = fall + RIPPLE_TIME;
        float phase = w.t + hash01(id * 3 + 2) * life;
        int cycle = (int)floorf(phase / life);
        float local = phase - cycle * life;
        float started = w.t - local;
        if (started < 0.15f + hash01(id * 3) * 0.7f) {
            continue;  // the rain starts a few drops at a time
        }
        float x0 = lerp(-1.1f, 1.1f, hash2(id, cycle)) - slant * (ground - top) * 0.5f;
        if (local < fall) {
            float y = top + local * layer->speed;
            float x = x0 + slant * (y - top);
            float length = minf(layer->length, y - top);
            float boost = 1 + 1.5f * w.flash;
            add_streak(x - slant * length, y - length, slant * length, length, layer->width, color, layer->alpha * boost);
            continue;
        }
        if (layer->ripple <= 0) {
            continue;
        }
        float age = (local - fall) / RIPPLE_TIME;
        float gx = x0 + slant * (ground - top);
        float fade = (1 - age) * (1 - age);
        add_ripple(gx, ground, layer->ripple * (0.15f + age), fade * layer->alpha * 1.2f, color);
        if (age > 0.25f) {
            add_ripple(gx, ground, layer->ripple * (age - 0.2f), fade * layer->alpha, color);
        }
        if (layer->ripple > 0.07f && age < 0.45f) {
            // two droplets jump out of the splash
            float a = age / 0.45f;
            for (int side = -1; side <= 1; side += 2) {
                float px = gx + side * layer->ripple * (0.2f + 0.6f * a);
                float py = ground - layer->ripple * 2.2f * a * (1 - a);
                add_dot(px, py, 1.3f * PX, 0.6f, color, (1 - a) * layer->alpha * 1.5f, 1);
            }
        }
    }
}

// Water drops sitting on the glass; a couple slide down in little jerks.
static void add_beads(int count, rgb_t tint) {
    for (int k = 0; k < count; k++) {
        float appear = 0.5f + hash01(k + 700) * 2.5f;
        if (w.t < appear) {
            continue;
        }
        float pop = ease_back((w.t - appear) / 0.25f);
        float x = lerp(-0.7f, 0.7f, hash01(k + 710));
        float y0 = lerp(-0.65f, 0.3f, hash01(k + 720));
        float r = 0.028f + 0.028f * hash01(k + 730);
        float y = y0;
        float slide = w.t - appear - 0.8f;
        if (k < 2 && slide > 0) {
            float steps = slide * 1.4f;
            float whole = floorf(steps);
            y = y0 + 0.1f * (whole + smoothstep(0.55f, 1.0f, steps - whole));
            if (y > 1.05f) {
                continue;
            }
            add_streak(x, y0, 0, y - y0, r * 0.35f, tint, 0.18f);
        }
        sprite_t *s = sprite_new(SP_BEAD, x, y, r * 1.7f);
        if (s != NULL) {
            s->a = r * pop;
            s->color = tint;
        }
    }
}

// --- Rain ---

static void build_rain(void) {
    float t = w.t;
    float drop = (1 - ease_back(t / 0.9f)) * -0.5f;  // clouds settle in from above
    cloud_style_t back = {{0.27f, 0.3f, 0.38f}, {0.1f, 0.12f, 0.17f}, 0.15f, 0.03f, 1, -3, -8, 0.015f};
    cloud_style_t front = {{0.46f, 0.5f, 0.6f}, {0.14f, 0.16f, 0.22f}, 0.35f, 0.012f, 1, -3, -8, 0.02f};
    rain_layer_t far = {80, 1.3f, 0.07f, 0.3f * PX, 0.4f, HORIZON + 0.01f, 0.46f, 0.035f};
    rain_layer_t mid = {44, 2.0f, 0.12f, 0.6f * PX, 0.55f, 0.47f, 0.7f, 0.06f};
    rain_layer_t near = {14, 2.9f, 0.24f, 1.3f * PX, 0.75f, 0.74f, 0.96f, 0.11f};
    rgb_t drop_color = {0.62f, 0.72f, 0.92f};
    const float slant = -0.1f;

    add_cloud_row(5, -0.8f + drop, 1.25f, t * 0.03f, 1, &back);
    add_rain(&far, 0, slant, -0.62f, drop_color);
    add_cloud_row(4, -0.56f + drop, 0.8f, -t * 0.05f, 2, &front);
    add_rain(&mid, 1000, slant, -0.5f, drop_color);
    add_rain(&near, 2000, slant, -0.45f, drop_color);
    add_beads(7, rgb(0.7f, 0.78f, 0.92f));
}

// Sky above the water line and the dark water below it, with its reflection.
static void water_sky_row(float v, int i0, int i1, rgb_t top, rgb_t low, rgb_t water, rgb_t deep, float lit) {
    rgb_t c;
    float shine = 0;
    if (v < HORIZON) {
        float k = sat((v + 1) / (HORIZON + 1));
        c = mix(top, low, k * k);
    } else {
        float depth = sat((v - HORIZON) / (1 - HORIZON));
        c = mix(water, deep, sqrtf(depth));
        float lines = sinf(v * 150 - w.t * 3) * sinf(v * 37 + w.t);
        c = scaled(c, 1 + 0.18f * lines * (1 - depth));
        shine = 0.12f * (1 - depth);
    }
    float flash = w.flash * lit * (0.5f - 0.25f * v);
    for (int i = i0; i < i1; i++) {
        float u = pixel_u(i);
        buf[i] = c;
        if (flash > 0) {
            // brightest around the strike
            float dx = u - w.flash_x;
            float k = flash * (0.25f + 0.75f / (1 + dx * dx * 6));
            add_color(&buf[i], rgb(0.45f, 0.45f, 0.75f), k);
        }
        if (shine > 0) {
            // brighter in the middle, where the sky reflects
            float k = shine / (1 + u * u * 4);
            add_color(&buf[i], low, k);
        }
    }
    // a soft mist line where the water meets the sky
    float mist = 0.08f / (1 + (v - HORIZON) * (v - HORIZON) * 2500);
    for (int i = i0; i < i1; i++) {
        add_color(&buf[i], low, mist);
    }
}

// --- Storm ---

// Lightning strikes at irregular times; finds the latest one and sets the flash.
static void storm_lightning(float *age, int *strike) {
    float s = 0.9f;
    float last = -10;
    int k = 0;
    *strike = -1;
    while (s <= w.t && k < 64) {
        last = s;
        *strike = k;
        s += 0.8f + 2.0f * hash01(k * 7 + 3);
        k++;
    }
    *age = w.t - last;
    float f = 0;
    if (*age < 0.06f) {
        f = 1;
    } else if (*age < 0.1f) {
        f = 0.3f;
    } else if (*age < 0.17f) {
        f = 0.9f;
    } else {
        f = 0.9f * expf(-(*age - 0.17f) * 7);
    }
    if (*strike >= 0 && hash01(*strike * 11 + 5) < 0.3f) {
        f *= 0.5f;  // a flicker inside the clouds, no bolt
    }
    w.flash = f;
    w.flash_x = lerp(-0.45f, 0.45f, hash01(*strike * 13 + 1));
}

static void add_bolt_segment(float x, float y, float nx, float ny, float width, float alpha) {
    const float reach = 0.045f;
    sprite_t *s = sprite_new(SP_BOLT, x, y, 0);
    if (s == NULL) {
        return;
    }
    s->ax = nx - x;
    s->ay = ny - y;
    s->a = width;
    s->b = reach;
    s->color = rgb(1, 0.98f, 1);
    s->color2 = rgb(0.6f, 0.55f, 1.0f);
    s->alpha = alpha;
    s->x0 = minf(x, nx) - reach * 5;
    s->x1 = maxf(x, nx) + reach * 5;
    s->y0 = minf(y, ny) - reach * 5;
    s->y1 = maxf(y, ny) + reach * 5;
}

// A jagged bolt from the clouds to the ground, with two forks.
static void add_bolt(int strike, float alpha) {
    float x = w.flash_x;
    float y = -0.4f;
    for (int n = 0; n < 9; n++) {
        float nx = x + (hash2(strike, n) - 0.5f) * 0.17f;
        float ny = y + 0.135f;
        add_bolt_segment(x, y, nx, ny, 1.4f * PX - n * 0.08f * PX, alpha);
        if (n == 2 || n == 5) {
            float side = 1;
            if (hash2(strike, n + 20) < 0.5f) {
                side = -1;
            }
            float bx = nx;
            float by = ny;
            for (int b = 0; b < 3; b++) {
                float ex = bx + side * (0.05f + 0.06f * hash2(strike, n * 5 + b + 30));
                float ey = by + 0.07f + 0.04f * hash2(strike, n * 5 + b + 40);
                add_bolt_segment(bx, by, ex, ey, 0.6f * PX, alpha * 0.6f);
                bx = ex;
                by = ey;
            }
        }
        x = nx;
        y = ny;
    }
    add_dot(x, y, 0.12f, 0, rgb(0.7f, 0.65f, 1.0f), alpha * 0.5f, 1);
}

static void build_storm(void) {
    float t = w.t;
    float age;
    int strike;
    storm_lightning(&age, &strike);
    float drop = (1 - ease_back(t / 0.9f)) * -0.6f;
    cloud_style_t back = {{0.2f, 0.2f, 0.3f}, {0.05f, 0.05f, 0.09f}, 0.15f, 0.03f, 1, -2, -8, 0.04f};
    cloud_style_t mid = {{0.3f, 0.3f, 0.42f}, {0.07f, 0.07f, 0.12f}, 0.25f, 0.02f, 1, -2, -8, 0.05f};
    cloud_style_t front = {{0.38f, 0.38f, 0.5f}, {0.06f, 0.06f, 0.1f}, 0.35f, 0.012f, 1, -2, -8, 0.06f};
    rain_layer_t far = {55, 1.8f, 0.12f, 0.35f * PX, 0.45f, HORIZON + 0.01f, 0.46f, 0.035f};
    rain_layer_t midr = {46, 2.6f, 0.18f, 0.7f * PX, 0.55f, 0.47f, 0.7f, 0.06f};
    rain_layer_t near = {18, 3.5f, 0.32f, 1.3f * PX, 0.7f, 0.74f, 0.96f, 0.1f};
    rgb_t drop_color = {0.6f, 0.65f, 0.85f};
    float slant = -0.32f + 0.08f * sinf(t * 0.9f);

    add_cloud_row(4, -0.85f + drop, 1.3f, t * 0.06f, 11, &back);
    add_rain(&far, 3000, slant, -0.5f, drop_color);
    add_cloud_row(4, -0.62f + drop, 1.0f, -t * 0.09f, 12, &mid);
    if (strike >= 0 && age < 0.45f && hash01(strike * 11 + 5) >= 0.3f) {
        add_bolt(strike, w.flash);
    }
    add_cloud_row(4, -0.42f + drop, 0.8f, t * 0.12f, 13, &front);
    add_rain(&midr, 4000, slant, -0.35f, drop_color);
    add_rain(&near, 5000, slant, -0.3f, drop_color);
}

// --- Snow ---

static float bank_height(float u) {
    float shape = 0.8f + 0.22f * sinf(u * 2.4f + 0.8f) + 0.12f * sinf(u * 6.1f + 2.1f) + 0.05f * sinf(u * 13 + 1);
    return 1.0f - w.bank * shape;
}

static void add_flakes(int count, uint32_t seed, float size0, float size1, float speed0, float speed1,
                       float sway, float alpha, int crystal) {
    for (int i = 0; i < count; i++) {
        uint32_t id = seed + i;
        float size = lerp(size0, size1, hash01(id * 5));
        float speed = lerp(speed0, speed1, hash01(id * 5 + 1));
        float x = lerp(-1.1f, 1.1f, hash01(id * 5 + 2)) + sway * sinf(w.t * (0.8f + hash01(id * 5 + 3)) + 6 * hash01(id));
        x = wrap(x + w.t * 0.04f, 2.2f);
        float y = -1.1f + fmodf(hash01(id * 5 + 4) * 2.3f + w.t * speed, 2.3f);
        float land = bank_height(x);
        if (y > land + size) {
            continue;
        }
        if (crystal) {
            sprite_t *s = sprite_new(SP_CRYSTAL, x, y, size + 0.03f);
            if (s != NULL) {
                s->a = size;
                s->b = 0.55f * PX;
                s->color = rgb(0.92f, 0.95f, 1.0f);
                s->alpha = alpha;
                sprite_rotate(s, w.t * (hash01(id) - 0.5f) * 2 + id);
            }
        } else {
            float hard = 0.5f;
            if (size > 0.04f) {
                hard = 0;  // big blurry flakes right in front of the glass
            }
            add_dot(x, y, size, hard, rgb(0.9f, 0.93f, 1.0f), alpha, 0);
        }
    }
}

static void build_snow(void) {
    w.bank = 0.18f + 0.12f * ease_out(w.t / 5);
    for (int i = 0; i < EYE_SIZE; i++) {
        w.ground[i] = bank_height(pixel_u(i));
    }
    add_flakes(60, 100, 0.7f * PX, 1.3f * PX, 0.06f, 0.1f, 0.02f, 0.6f, 0);
    w.split = sprites_count();
    // glints twinkling on the snow bank
    for (int k = 0; k < 7; k++) {
        float x = lerp(-0.6f, 0.6f, hash01(k + 900));
        float y = bank_height(x) + 0.02f + 0.06f * hash01(k + 910);
        float tw = sinf(w.t * (2 + hash01(k + 920) * 2) + k * 2.1f);
        if (tw <= 0) {
            continue;
        }
        sprite_t *s = sprite_new(SP_GLINT, x, y, 0.04f);
        if (s != NULL) {
            s->a = 0.035f * tw;
            s->color = rgb(0.8f, 0.9f, 1.0f);
            s->alpha = 0.8f;
            sprite_rotate(s, 0.3f);
        }
    }
    add_flakes(44, 200, 1.8f * PX, 3.6f * PX, 0.12f, 0.17f, 0.04f, 0.85f, 0);
    add_flakes(10, 300, 0.035f, 0.065f, 0.16f, 0.24f, 0.07f, 0.95f, 1);
    add_flakes(4, 400, 0.06f, 0.09f, 0.28f, 0.34f, 0.1f, 0.16f, 0);
}

// A pine tree: three stacked tiers with snow on top of each.
static void draw_pine(rgb_t *c, float u, float v, float bx, float top, float h, rgb_t body, rgb_t snow) {
    float dx = fabsf(u - bx);
    if (dx > h * 0.4f || v < top - PX || v > top + h) {
        return;
    }
    float cover = 0;
    float snowy = 0;
    for (int k = 0; k < 3; k++) {
        float apex = top + k * h * 0.24f;
        float base = apex + h * 0.4f;
        float hw = h * (0.17f + 0.07f * k);
        float y = (v - apex) / (base - apex);
        if (y < 0 || y > 1) {
            continue;
        }
        float edge = fill(dx - hw * y, PX * 1.2f) * fill(v - base, PX);
        if (edge > cover) {
            cover = edge;
            snowy = 1 - smoothstep(0.15f, 0.35f, y + 0.04f * sinf(u * 90));
        }
    }
    float trunk = fill(dx - h * 0.04f, PX) * fill(v - top - h, PX);
    cover = maxf(cover, trunk);
    over(c, mix(body, snow, snowy), cover);
}

static void snow_ground_row(float v, int i0, int i1) {
    rgb_t far_pine = {0.14f, 0.2f, 0.34f};
    rgb_t near_pine = {0.05f, 0.09f, 0.18f};
    rgb_t snow_lit = {0.88f, 0.92f, 1.0f};
    rgb_t snow_shade = {0.42f, 0.52f, 0.76f};
    for (int i = i0; i < i1; i++) {
        float u = pixel_u(i);
        draw_pine(&buf[i], u, v, 0.55f, 0.36f, 0.42f, far_pine, rgb(0.6f, 0.68f, 0.85f));
        draw_pine(&buf[i], u, v, -0.6f, 0.2f, 0.62f, near_pine, rgb(0.75f, 0.82f, 0.98f));
        float top = w.ground[i];
        if (v < top - PX) {
            continue;
        }
        float slope = 0;
        if (i > 0 && i < EYE_SIZE - 1) {
            slope = (w.ground[i + 1] - w.ground[i - 1]) * HALF / 2;
        }
        float depth = v - top;
        rgb_t c = mix(snow_lit, snow_shade, sat(depth * 5 + slope * 1.5f));
        float rim = sat(1 - depth / (3 * PX));
        add_color(&c, WHITE, rim * 0.15f);
        over(&buf[i], c, fill(top - v, PX));
    }
}

// --- Sun ---

static float far_hill(float u) {
    return 0.6f + 0.05f * sinf(1.7f * u + 0.4f) + 0.025f * sinf(4.3f * u + 1.1f);
}

static float near_hill(float u) {
    return 0.76f + 0.07f * sinf(1.3f * u + 2.0f) + 0.03f * sinf(3.9f * u + 0.3f);
}

static void build_sun(void) {
    float t = w.t;
    w.sun_x = 0.14f;
    w.sun_y = -0.24f;
    w.sun_r = 0.2f * ease_back(t / 0.8f) * (1 + 0.02f * sinf(t * 2.2f));
    w.rays = ease_out((t - 0.2f) / 1.0f);
    float a1 = t * 0.06f;
    float a2 = -t * 0.04f + PI / 12;
    w.ray_c1 = cosf(12 * a1);
    w.ray_s1 = sinf(12 * a1);
    w.ray_c2 = cosf(12 * a2);
    w.ray_s2 = sinf(12 * a2);
    for (int i = 0; i < EYE_SIZE; i++) {
        float u = pixel_u(i);
        w.ground[i] = near_hill(u);
        w.ground2[i] = far_hill(u);
    }

    // lazy clouds drifting past, lit from the sun's side
    cloud_style_t puffy = {{1.0f, 0.98f, 0.94f}, {0.68f, 0.74f, 0.9f}, 0.5f, 0.012f, 1, w.sun_x, w.sun_y, 0.012f};
    cloud_style_t faint = {{0.92f, 0.94f, 1.0f}, {0.62f, 0.7f, 0.9f}, 0.3f, 0.02f, 0.7f, w.sun_x, w.sun_y, 0.01f};
    add_cloud(0.85f - t * 0.04f, -0.62f, 0.32f, 77, &faint);
    add_cloud(-0.62f + t * 0.06f, 0.08f, 0.5f, 78, &puffy);

    // two birds flapping across
    for (int b = 0; b < 2; b++) {
        float x = -0.75f + t * 0.1f + b * 0.12f;
        float y = 0.28f - b * 0.06f + 0.015f * sinf(t * 2 + b);
        sprite_t *s = sprite_new(SP_BIRD, x, y, 0.04f);
        if (s != NULL) {
            s->a = 0.026f - b * 0.006f;
            s->b = sinf(t * 9 + b * 1.3f);
            s->color = rgb(0.25f, 0.22f, 0.32f);
            s->alpha = 0.8f;
        }
    }

    // lens flare: ghosts on the line from the sun through the center
    float gx = -w.sun_x;
    float gy = -w.sun_y;
    float glow = w.rays;
    add_dot(w.sun_x + gx * 1.55f, w.sun_y + gy * 1.55f, 0.035f, 0.5f, rgb(1, 0.8f, 0.5f), 0.14f * glow, 1);
    add_dot(w.sun_x + gx * 2.1f, w.sun_y + gy * 2.1f, 0.075f, 0.7f, rgb(0.5f, 1, 0.75f), 0.07f * glow, 1);
    add_dot(w.sun_x + gx * 2.7f, w.sun_y + gy * 2.7f, 0.05f, 0.4f, rgb(0.75f, 0.6f, 1), 0.1f * glow, 1);
}

static void sun_row(float v, int j, int i0, int i1) {
    rgb_t top = {0.08f, 0.3f, 0.72f};
    rgb_t mid = {0.42f, 0.66f, 0.95f};
    rgb_t low = {1.0f, 0.84f, 0.66f};
    rgb_t warm = {1.0f, 0.82f, 0.5f};
    rgb_t core = {1.0f, 0.99f, 0.9f};
    rgb_t limb = {1.0f, 0.74f, 0.25f};
    rgb_t far_color = {0.56f, 0.6f, 0.8f};
    rgb_t near_color = {0.26f, 0.5f, 0.38f};
    rgb_t near_lit = {0.55f, 0.75f, 0.42f};
    rgb_t sky;
    if (v < 0.1f) {
        sky = mix(top, mid, sat((v + 1) / 1.1f));
    } else {
        sky = mix(mid, low, sat((v - 0.1f) / 0.6f));
    }
    float dy = v - w.sun_y;
    float r = w.sun_r;
    // heat shimmer: the far hill's edge wobbles
    int shift = (int)(3 * sinf(v * 80 + w.t * 9) * smoothstep(0.45f, 0.62f, v));
    for (int i = i0; i < i1; i++) {
        float u = pixel_u(i);
        float dx = u - w.sun_x;
        float d = len(dx, dy);
        rgb_t c = sky;
        float edge = d - r;
        add_color(&c, warm, 0.6f * halo(edge, 0.08f) + 0.35f * halo(edge, 0.45f));
        if (edge > -PX && w.rays > 0) {
            // cos(12 angle) without atan: the 12th power of the unit direction
            float zx = dx / d;
            float zy = dy / d;
            float x2 = zx * zx - zy * zy;
            float y2 = 2 * zx * zy;
            float x4 = x2 * x2 - y2 * y2;
            float y4 = 2 * x2 * y2;
            float x8 = x4 * x4 - y4 * y4;
            float y8 = 2 * x4 * y4;
            float x12 = x8 * x4 - y8 * y4;
            float y12 = x8 * y4 + y8 * x4;
            float long_ray = sat(x12 * w.ray_c1 - y12 * w.ray_s1);
            float short_ray = sat(x12 * w.ray_c2 - y12 * w.ray_s2);
            long_ray = long_ray * long_ray * long_ray * long_ray;
            short_ray = short_ray * short_ray;
            float reach = w.rays * w.rays;
            float lit = long_ray * 0.22f * halo(edge, 0.45f * reach + 0.01f) + short_ray * 0.25f * halo(edge, 0.12f * reach + 0.01f);
            add_color(&c, warm, lit * w.rays);
        }
        float disc = fill(edge, PX);
        if (disc > 0) {
            float k = sat(1 - d * d / (r * r));
            over(&c, mix(limb, core, sqrtf(k)), disc);
        }
        int fi = i + shift;
        if (fi < 0) {
            fi = 0;
        }
        if (fi >= EYE_SIZE) {
            fi = EYE_SIZE - 1;
        }
        float far_top = w.ground2[fi];
        if (v > far_top - PX) {
            rgb_t hill = mix(mix(far_color, low, 0.25f), far_color, sat((v - far_top) * 6));
            over(&c, hill, fill(far_top - v, PX));
        }
        float near_top = w.ground[i];
        if (v > near_top - PX) {
            rgb_t hill = mix(near_lit, near_color, sat((v - near_top) * 7));
            over(&c, hill, fill(near_top - v, PX));
        }
        buf[i] = c;
    }
    (void)j;
}

// --- Cloud ---

static void build_cloud(void) {
    float t = w.t;
    float in = 1 - ease_out(t / 1.2f);  // layers slide in from opposite sides
    float sx = 0.38f;
    float sy = -0.55f;
    cloud_style_t far = {{0.7f, 0.75f, 0.84f}, {0.46f, 0.52f, 0.64f}, 0.3f, 0.025f, 0.85f, sx, sy, 0.01f};
    cloud_style_t mid = {{0.84f, 0.87f, 0.93f}, {0.5f, 0.56f, 0.68f}, 0.45f, 0.018f, 1, sx, sy, 0.012f};
    cloud_style_t near = {{0.97f, 0.97f, 1.0f}, {0.5f, 0.55f, 0.68f}, 0.55f, 0.012f, 1, sx, sy, 0.015f};
    add_cloud_row(4, -0.7f, 0.55f, t * 0.02f + in * 0.5f, 21, &far);
    add_cloud_row(3, -0.15f, 0.72f, -t * 0.035f - in * 0.8f, 22, &mid);
    add_cloud_row(3, 0.52f, 0.95f, t * 0.06f + in * 1.2f, 24, &near);
}

static void cloud_row(float v, int i0, int i1) {
    rgb_t top = {0.34f, 0.43f, 0.58f};
    rgb_t low = {0.64f, 0.7f, 0.8f};
    rgb_t warm = {1.0f, 0.94f, 0.82f};
    rgb_t sky = mix(top, low, sat((v + 1) / 2));
    float sx = 0.38f;
    float sy = -0.55f;
    float dy = v - sy;
    float phase = w.t * 0.15f;
    float c1 = cosf(phase);
    float s1 = sinf(phase);
    for (int i = i0; i < i1; i++) {
        float dx = pixel_u(i) - sx;
        float d = len(dx, dy) + 0.0001f;
        rgb_t c = sky;
        add_color(&c, warm, 0.45f * halo(d, 0.12f) + 0.25f * halo(d, 0.6f));
        if (dy > 0) {
            // light shafts fanning down from the hidden sun
            float zx = dx / d;
            float zy = dy / d;
            float x2 = zx * zx - zy * zy;
            float y2 = 2 * zx * zy;
            float x4 = x2 * x2 - y2 * y2;
            float y4 = 2 * x2 * y2;
            float x8 = x4 * x4 - y4 * y4;
            float y8 = 2 * x4 * y4;
            float x16 = x8 * x8 - y8 * y8;
            float y16 = 2 * x8 * y8;
            float shaft = sat(x16 * c1 - y16 * s1);
            add_color(&c, warm, shaft * shaft * 0.16f * zy * halo(d, 0.9f));
        }
        buf[i] = c;
    }
}

// --- Frame ---

void weather_update(int scene, float t, float left) {
    w.scene = scene;
    w.t = t;
    w.flash = 0;
    w.split = 0;
    float open = ease_back(t / OPEN_TIME);
    float close = smoothstep(0, CLOSE_TIME, left);
    w.reveal = 1.5f * minf(open, close);
    sprites_clear();
    switch (scene) {
    case SCENE_RAIN:
        build_rain();
        break;
    case SCENE_STORM:
        build_storm();
        break;
    case SCENE_SNOW:
        build_snow();
        break;
    case SCENE_SUN:
        build_sun();
        break;
    case SCENE_CLOUD:
        build_cloud();
        break;
    }
}

int weather_covers(void) {
    return w.reveal >= 1.15f;
}

void weather_draw(rgb_t *row, int j) {
    float v = row_v(j);
    int i0;
    int i1;
    row_span(v, -1, 1, &i0, &i1);
    if (w.reveal <= 0 || i0 >= i1) {
        return;
    }
    switch (w.scene) {
    case SCENE_RAIN:
        water_sky_row(v, i0, i1, rgb(0.07f, 0.09f, 0.14f), rgb(0.26f, 0.3f, 0.38f), rgb(0.16f, 0.19f, 0.25f),
                      rgb(0.04f, 0.05f, 0.08f), 0);
        break;
    case SCENE_STORM:
        water_sky_row(v, i0, i1, rgb(0.03f, 0.03f, 0.07f), rgb(0.13f, 0.12f, 0.2f), rgb(0.08f, 0.08f, 0.12f),
                      rgb(0.02f, 0.02f, 0.04f), 1);
        break;
    case SCENE_SNOW: {
        rgb_t top = {0.03f, 0.06f, 0.16f};
        rgb_t low = {0.24f, 0.3f, 0.47f};
        rgb_t sky = mix(top, low, sat((v + 1) / 1.7f));
        float dy = v + 0.5f;
        for (int i = i0; i < i1; i++) {
            // soft cold light from behind the snow clouds
            float dx = pixel_u(i) - 0.4f;
            buf[i] = sky;
            add_color(&buf[i], rgb(0.35f, 0.45f, 0.7f), 0.35f / (1 + (dx * dx + dy * dy) * 6));
        }
        break;
    }
    case SCENE_SUN:
        sun_row(v, j, i0, i1);
        break;
    case SCENE_CLOUD:
        cloud_row(v, i0, i1);
        break;
    }
    sprites_draw(buf, j, 0, w.split);
    if (w.scene == SCENE_SNOW) {
        snow_ground_row(v, i0, i1);
    }
    sprites_draw(buf, j, w.split, sprites_count());

    for (int i = i0; i < i1; i++) {
        float u = pixel_u(i);
        float r2 = u * u + v * v;
        glass_shine(&buf[i], u, v, r2, 1);
        if (w.reveal >= 1.45f) {
            row[i] = buf[i];
            continue;
        }
        float m = sat((w.reveal - sqrtf(r2)) / 0.1f);
        over(&row[i], buf[i], m);
    }
}
