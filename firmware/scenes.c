// Scenes that keep the eyes: confetti, sparkles and zzz. Weather lives in weather.c.
#include "scene.h"
#include "sprites.h"
#include "weather.h"

static struct {
    int scene;
    int weather;
    float t;
    float fade;
    int split;        // sprites before this are behind the eyes
    float night;      // zzz: how much the glass turns to night
    float breath;     // zzz: 0..1 breathing
} sc;

static const rgb_t WHITE = {1, 1, 1};

static const rgb_t CONFETTI[8] = {
    {1.0f, 0.3f, 0.55f},
    {1.0f, 0.8f, 0.2f},
    {0.2f, 0.8f, 1.0f},
    {0.55f, 0.95f, 0.3f},
    {1.0f, 0.55f, 0.15f},
    {0.65f, 0.4f, 1.0f},
    {0.95f, 0.95f, 1.0f},
    {1.0f, 0.35f, 0.3f},
};

static const rgb_t GLINT_COLORS[4] = {
    {1.0f, 1.0f, 1.0f},
    {1.0f, 0.85f, 0.45f},
    {1.0f, 0.7f, 0.85f},
    {0.6f, 0.9f, 1.0f},
};

int scene_is_weather(int scene) {
    if (scene == SCENE_RAIN || scene == SCENE_SNOW || scene == SCENE_SUN) {
        return 1;
    }
    if (scene == SCENE_STORM || scene == SCENE_CLOUD) {
        return 1;
    }
    return 0;
}

// --- Confetti ---

// One tumbling piece. Its width follows cos(flip) so it looks like it turns over in 3D.
static void confetti_piece(uint32_t id, float x, float y, float depth, float t) {
    float h = hash01(id * 7);
    float spin = 2 + 3 * hash01(id * 7 + 1);
    if (hash01(id * 7 + 2) < 0.5f) {
        spin = -spin;
    }
    float angle = h * 6 + t * spin;
    float flip = cosf(t * (4 + 5 * hash01(id * 7 + 3)) + h * 6);
    float facing = fabsf(flip);
    rgb_t color = CONFETTI[id % 8];
    if (flip < 0) {
        color = mix(color, WHITE, 0.3f);  // the back side is paler
    }
    color = scaled(color, (0.5f + 0.5f * facing) * (0.7f + 0.3f * depth));
    float size = (0.022f + 0.016f * hash01(id * 7 + 4)) * depth;
    float shape = hash01(id * 7 + 5);
    sprite_t *s;
    if (shape < 0.68f) {
        s = sprite_new(SP_PAPER, x, y, size * 1.5f);
        if (s == NULL) {
            return;
        }
        s->a = size * maxf(facing, 0.12f);
        s->b = size * 0.6f;
    } else if (shape < 0.88f) {
        s = sprite_new(SP_SEQUIN, x, y, size);
        if (s == NULL) {
            return;
        }
        s->a = size * 0.7f * maxf(facing, 0.15f);
        s->b = size * 0.7f;
    } else {
        s = sprite_new(SP_PAPER, x, y, size * 2.4f);
        if (s == NULL) {
            return;
        }
        s->a = size * 0.35f * maxf(facing, 0.2f);
        s->b = size * 2.2f;
    }
    sprite_rotate(s, angle);
    s->color = color;
    s->alpha = sc.fade;
}

// Two party poppers fire up from the bottom corners; drag slows the pieces and they drift down.
static void confetti_burst(int front) {
    const float drag = 2.2f;
    const float gravity = 0.8f;
    for (int i = 0; i < 96; i++) {
        uint32_t id = i + 1;
        float depth = 0.7f + 0.75f * hash01(id * 3 + 100);
        if ((depth > 1.2f) != front) {
            continue;
        }
        float side = -1;
        if (i % 2 == 1) {
            side = 1;
        }
        float t = sc.t - 0.15f - 0.08f * hash01(id + 50);
        if (t <= 0) {
            continue;
        }
        float angle = (50 + 38 * hash01(id * 3 + 101)) * PI / 180;
        float speed = 2.8f + 2.4f * hash01(id * 3 + 102);
        float vx = -side * speed * cosf(angle);
        float vy = -speed * sinf(angle);
        float e = 1 - expf(-drag * t);
        float x = side * 0.62f + vx * e / drag;
        float y = 0.95f + vy * e / drag + gravity / drag * (t - e / drag);
        x += 0.07f * sinf(t * (3 + 2 * hash01(id)) + id) * e;
        if (y > 1.1f) {
            continue;
        }
        confetti_piece(id, x, y, depth, sc.t);
    }
}

// After the burst, more confetti keeps fluttering down from the top.
static void confetti_shower(int front) {
    for (int i = 0; i < 44; i++) {
        uint32_t id = 200 + i;
        float depth = 0.7f + 0.75f * hash01(id * 3 + 100);
        if ((depth > 1.2f) != front) {
            continue;
        }
        float speed = (0.26f + 0.14f * hash01(id * 3 + 1)) * depth;
        float life = 2.4f / speed;
        float start = 0.7f + 2.0f * hash01(id);
        if (sc.t < start) {
            continue;
        }
        float phase = sc.t - start;
        int cycle = (int)floorf(phase / life);
        float local = phase - cycle * life;
        float x = lerp(-0.95f, 0.95f, hash2(id, cycle)) + 0.07f * sinf(sc.t * (2 + hash01(id + 9)) + id);
        float y = -1.15f + local * speed;
        confetti_piece(id, x, y, depth, sc.t);
    }
}

// A flash of light and a few sparks where each popper fires.
static void popper_flash(void) {
    float t = sc.t - 0.15f;
    if (t < 0 || t > 0.5f) {
        return;
    }
    float k = 1 - t / 0.5f;
    for (int side = -1; side <= 1; side += 2) {
        sprite_t *s = sprite_new(SP_DOT, side * 0.62f, 0.95f, 0.45f);
        if (s != NULL) {
            s->a = 0.45f;
            s->b = 0;
            s->add = 1;
            s->color = rgb(1, 0.85f, 0.6f);
            s->alpha = 0.55f * k * k * sc.fade;
        }
        float e = 1 - expf(-6 * t);
        for (int n = 0; n < 6; n++) {
            float angle = (60 + 25 * hash01(n + 70)) * PI / 180;
            float dist = (0.25f + 0.35f * hash01(n + 80)) * e;
            float x = side * 0.62f - side * cosf(angle) * dist;
            float y = 0.95f - sinf(angle) * dist;
            sprite_t *g = sprite_new(SP_GLINT, x, y, 0.06f);
            if (g != NULL) {
                g->a = 0.06f * k;
                g->color = rgb(1, 0.9f, 0.6f);
                g->alpha = sc.fade;
            }
        }
    }
}

static void build_confetti(void) {
    popper_flash();
    confetti_burst(0);
    confetti_shower(0);
    sc.split = sprites_count();
    confetti_burst(1);
    confetti_shower(1);
}

// --- Sparkles ---

static void add_glint(float x, float y, float size, float angle, rgb_t color, float alpha) {
    sprite_t *s = sprite_new(SP_GLINT, x, y, size);
    if (s != NULL) {
        s->a = size;
        s->color = color;
        s->alpha = alpha;
        sprite_rotate(s, angle);
    }
}

static void sparkle_glints(int front) {
    for (int k = 0; k < 30; k++) {
        int big = k < 6;
        if (big != front) {
            continue;
        }
        float delay = hash01(k + 30) * 0.5f;
        float t = sc.t - delay;
        if (t < 0) {
            continue;
        }
        float period = 1.6f + 1.2f * hash01(k + 40);
        int cycle = (int)floorf(t / period);
        float p = t / period - cycle;
        float x = lerp(-0.82f, 0.82f, hash2(k, cycle));
        float y = lerp(-0.8f, 0.7f, hash2(k + 50, cycle));
        if (x * x + y * y > 0.75f) {
            x *= 0.7f;
            y *= 0.7f;
        }
        if (cycle == 0) {
            // the first ones burst out from the middle
            float out = ease_out(p * 2.5f);
            x *= out;
            y = lerp(-0.05f, y, out);
        }
        y -= p * 0.1f;
        float grow = ease_back(p / 0.22f);
        float shrink = 1 - smoothstep(0.55f, 1.0f, p);
        float shimmer = 0.85f + 0.15f * sinf(sc.t * 23 + k * 3);
        float size = (0.05f + 0.07f * hash01(k + 60)) * grow * shrink * shimmer;
        if (big) {
            size *= 1.8f;
        }
        if (size < 0.004f) {
            continue;
        }
        float angle = hash01(k + 70) * 0.8f + sc.t * (hash01(k + 80) - 0.5f);
        add_glint(x, y, size, angle, GLINT_COLORS[k % 4], sc.fade);
    }
}

static void sparkle_dust(void) {
    for (int k = 0; k < 30; k++) {
        float x = lerp(-0.9f, 0.9f, hash01(k + 300)) + 0.03f * sinf(sc.t + k);
        float y = wrap(lerp(-1, 1, hash01(k + 310)) - sc.t * (0.04f + 0.05f * hash01(k + 320)), 2.0f);
        float tw = sinf(sc.t * (2 + 3 * hash01(k + 330)) + k);
        float alpha = tw * tw * sc.fade;
        sprite_t *s = sprite_new(SP_DOT, x, y, 3 * PX);
        if (s != NULL) {
            s->a = (0.6f + 0.8f * hash01(k + 340)) * PX;
            s->b = 0.3f;
            s->add = 1;
            s->color = rgb(1, 0.92f, 0.75f);
            s->alpha = alpha * 0.9f;
        }
    }
}

static void build_sparkles(void) {
    sparkle_dust();
    sparkle_glints(0);
    sc.split = sprites_count();
    sparkle_glints(1);
}

// --- Zzz ---

static void add_moon(void) {
    sprite_t *moon = sprite_new(SP_MOON, -0.5f, -0.56f, 0.26f);
    if (moon != NULL) {
        moon->a = 0.085f;
        moon->color = rgb(1.0f, 0.95f, 0.8f);
        moon->color2 = rgb(0.5f, 0.55f, 0.9f);
        moon->alpha = sc.fade;
    }
}

static void build_zzz(void) {
    sc.night = sc.fade;
    sc.breath = 0.5f + 0.5f * sinf(sc.t * 2 * PI / 3.2f);
    for (int k = 0; k < 12; k++) {
        float tw = 0.5f + 0.5f * sinf(sc.t * (1 + hash01(k + 510)) + k * 2);
        sprite_t *s = sprite_new(SP_DOT, lerp(-0.75f, 0.75f, hash01(k + 500)), lerp(-0.8f, -0.25f, hash01(k + 520)), 3 * PX);
        if (s != NULL) {
            s->a = (0.6f + 0.7f * hash01(k + 530)) * PX;
            s->b = 0.4f;
            s->add = 1;
            s->color = rgb(0.75f, 0.8f, 1.0f);
            s->alpha = tw * 0.8f * sc.fade;
        }
    }
    add_moon();
    sc.split = sprites_count();

    // Z letters float up from the sleeping face, growing, swaying and fading
    const float every = 1.1f;
    int newest = (int)floorf((sc.t - 0.3f) / every);
    for (int k = newest - 3; k <= newest; k++) {
        if (k < 0) {
            continue;
        }
        float age = sc.t - 0.3f - k * every;
        float life = age / 3.3f;
        if (life < 0 || life > 1) {
            continue;
        }
        float x = 0.3f + life * 0.3f + 0.05f * sinf(age * 2.2f + k);
        float y = -0.3f - life * 0.58f;
        float size = 0.025f + 0.05f * ease_out(life * 1.4f);
        sprite_t *s = sprite_new(SP_ZED, x, y, size * 2.4f);
        if (s == NULL) {
            return;
        }
        s->a = size;
        s->b = size * 0.2f;
        s->color = rgb(0.72f, 0.78f, 1.0f);
        s->alpha = sc.fade * smoothstep(0, 0.1f, life) * (1 - smoothstep(0.65f, 1.0f, life));
        sprite_rotate(s, 0.25f * sinf(age * 1.8f + k) - 0.12f);
    }
}

// Night tint and a slow breathing glow around the rim.
static void zzz_row(rgb_t *row, int j) {
    float v = row_v(j);
    int i0;
    int i1;
    row_span(v, -1, 1, &i0, &i1);
    float tint = sc.night * (0.035f - 0.02f * v);
    float breath = sc.night * (0.04f + 0.08f * sc.breath);
    for (int i = i0; i < i1; i++) {
        float u = pixel_u(i);
        float r2 = u * u + v * v;
        row[i].g += tint * 0.4f;
        row[i].b += tint;
        if (r2 > 0.7f) {
            float d = (r2 - 0.9f) * 12;
            float k = breath / (1 + d * d);
            add_color(&row[i], rgb(0.35f, 0.45f, 1.0f), k);
        }
    }
}

// --- Dispatch ---

void scene_update(int scene, float t, float left) {
    sc.scene = scene;
    sc.weather = scene_is_weather(scene);
    sc.t = t;
    sc.night = 0;
    if (sc.weather) {
        weather_update(scene, t, left);
        return;
    }
    sc.fade = minf(smoothstep(0, 0.25f, t), smoothstep(0, 0.35f, left));
    sc.split = 0;
    sprites_clear();
    switch (scene) {
    case SCENE_CONFETTI:
        build_confetti();
        break;
    case SCENE_SPARKLES:
        build_sparkles();
        break;
    case SCENE_ZZZ:
        build_zzz();
        break;
    }
}

int scene_covers(void) {
    if (sc.weather) {
        return weather_covers();
    }
    return 0;
}

void scene_draw_back(rgb_t *row, int j) {
    if (sc.weather) {
        weather_draw(row, j);
        return;
    }
    if (sc.night > 0) {
        zzz_row(row, j);
    }
    sprites_draw(row, j, 0, sc.split);
}

void scene_draw_front(rgb_t *row, int j) {
    if (sc.weather) {
        return;
    }
    sprites_draw(row, j, sc.split, sprites_count());
}
