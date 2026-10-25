// Small shapes that scenes and cards place each frame (raindrops, clouds, confetti, ...).
// Each is drawn only over the pixels it covers, in the order it was added.
#pragma once
#include "gfx.h"
#include <stddef.h>

#define MAX_SPRITES 360
#define MAX_CLOUDS 32
#define CLOUD_PUFFS 8

enum {
    SP_CLOUD,    // puffs merged into one cloud (add with cloud_puff): a edge softness, c rim light,
                 // d cloud base y, e cloud height; ax,ay toward the light; color lit, color2 shade
    SP_STREAK,   // falling streak from (x,y) to (x+ax, y+ay), brightest at the end: a half width
    SP_DOT,      // round dot: a radius, b hardness 0..1
    SP_CRYSTAL,  // six-armed snowflake: a arm length, b line half width; ax,ay rotation
    SP_RIPPLE,   // flat ring on water: a radius, b height/width, c line half width
    SP_BEAD,     // water drop on the glass: a radius
    SP_BOLT,     // lightning segment from (x,y) to (x+ax, y+ay): a core half width, b glow reach; color2 glow
    SP_PAPER,    // confetti rectangle: a, b half sizes; ax,ay rotation
    SP_SEQUIN,   // round confetti: a, b radii; ax,ay rotation
    SP_GLINT,    // four-point star: a arm length; ax,ay rotation
    SP_ZED,      // the letter Z: a half size, b line half width; ax,ay rotation
    SP_RING,     // glowing ring: a radius, b half width
    SP_HEART,    // small heart: a size; color top, color2 bottom
    SP_NOTE,     // music note: a size; ax,ay rotation
    SP_BIRD,     // distant bird: a wing span, b wing lift (-1..1)
    SP_MOON,     // crescent moon: a radius; color2 glow
};

typedef struct {
    int kind;
    int add;                 // 1 adds light, 0 paints over
    float x, y;
    float ax, ay;            // a vector or a rotation (cos, sin), depending on kind
    float a, b, c, d, e;     // sizes, depending on kind
    rgb_t color, color2;
    float alpha;
    float x0, x1, y0, y1;    // bounds in screen units
} sprite_t;

void sprites_clear(void);
int sprites_count(void);
// New sprite at (x, y) whose drawing fits within reach of it. NULL when full.
sprite_t *sprite_new(int kind, float x, float y, float reach);
void sprite_rotate(sprite_t *s, float angle);
// New empty cloud; give it puffs with cloud_puff. NULL when full.
sprite_t *sprite_cloud(void);
void cloud_puff(sprite_t *s, float x, float y, float r);
// Draws sprites first..last-1 onto row j.
void sprites_draw(rgb_t *row, int j, int first, int last);
