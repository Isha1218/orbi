// One line of text from the bitmap fonts in font.h.
#pragma once
#include <stdint.h>

#define MAX_GLYPHS 32
#define FONT_SMALL 0
#define FONT_BIG 1

typedef struct {
    int font;
    int count;
    int glyph[MAX_GLYPHS];   // index into the font's glyph table
    float x[MAX_GLYPHS];     // left edge of each bitmap, pixels
    float top;               // line top, pixels
    float scale;
    float x0, x1, y0, y1;    // ink bounds, pixels
} text_t;

// Lays out one line centered on (cx, cy) in pixels, shrunk if it is wider than max_w.
void text_layout(text_t *t, int font, const char *s, float cx, float cy, float max_w);
// Coverage of the text at pixel center (px, py), 0..1.
float text_alpha(const text_t *t, float px, float py);
int text_line_height(int font);
