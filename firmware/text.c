#include "text.h"
#include "font.h"
#include <math.h>
#include <string.h>

typedef struct {
    const font_glyph_t *glyphs;
    const uint8_t *alpha;
    int count;
    int height;
} font_t;

static const font_t FONTS[2] = {
    {FONT_SMALL_GLYPHS, FONT_SMALL_ALPHA, FONT_SMALL_COUNT, FONT_SMALL_HEIGHT},
    {FONT_BIG_GLYPHS, FONT_BIG_ALPHA, FONT_BIG_COUNT, FONT_BIG_HEIGHT},
};

static float minf(float a, float b) {
    return a < b ? a : b;
}

static float maxf(float a, float b) {
    return a > b ? a : b;
}

static float lerp(float a, float b, float k) {
    return a + (b - a) * k;
}

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

static int find_glyph(const font_t *f, uint32_t code) {
    for (int i = 0; i < f->count; i++) {
        if (f->glyphs[i].code == code) {
            return i;
        }
    }
    return -1;
}

int text_line_height(int font) {
    return FONTS[font].height;
}

void text_layout(text_t *t, int font, const char *s, float cx, float cy, float max_w) {
    const font_t *f = &FONTS[font];
    memset(t, 0, sizeof *t);
    t->font = font;
    if (s == NULL) {
        return;
    }
    int fallback = find_glyph(f, '?');
    int cap = find_glyph(f, 'H');  // lines are centered on the capital height
    float pen = 0;
    float ink_top = 1000;
    float ink_bottom = -1000;
    while (*s && t->count < MAX_GLYPHS) {
        uint32_t code = next_codepoint(&s);
        int index = find_glyph(f, code);
        if (index < 0) {
            index = fallback;
        }
        if (index < 0) {
            continue;
        }
        const font_glyph_t *g = &f->glyphs[index];
        t->glyph[t->count] = index;
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
    if (cap >= 0) {
        middle = f->glyphs[cap].y_off + f->glyphs[cap].h / 2.0f;
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
        t->x1 = maxf(t->x1, t->x[i] + f->glyphs[t->glyph[i]].w * t->scale);
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

// Bilinear sample of a glyph bitmap at (gx, gy), in glyph pixels.
static float glyph_sample(const font_t *f, const font_glyph_t *g, float gx, float gy) {
    int ix = (int)floorf(gx);
    int iy = (int)floorf(gy);
    float fx = gx - ix;
    float fy = gy - iy;
    float top = lerp(glyph_pixel(f, g, ix, iy), glyph_pixel(f, g, ix + 1, iy), fx);
    float bottom = lerp(glyph_pixel(f, g, ix, iy + 1), glyph_pixel(f, g, ix + 1, iy + 1), fx);
    return lerp(top, bottom, fy);
}

float text_alpha(const text_t *t, float px, float py) {
    if (px < t->x0 || px > t->x1 || py < t->y0 || py > t->y1) {
        return 0;
    }
    const font_t *f = &FONTS[t->font];
    float total = 0;
    for (int i = 0; i < t->count; i++) {
        const font_glyph_t *g = &f->glyphs[t->glyph[i]];
        float gx = (px - t->x[i]) / t->scale - 0.5f;
        if (gx < -1 || gx > g->w) {
            continue;
        }
        float gy = (py - t->top) / t->scale - g->y_off - 0.5f;
        if (gy < -1 || gy > g->h) {
            continue;
        }
        total += glyph_sample(f, g, gx, gy);
    }
    return minf(total, 1);
}
