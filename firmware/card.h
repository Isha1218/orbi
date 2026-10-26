// Glance cards: an animated illustration (icon, initials or photo) in place of the eyes.
#pragma once
#include "gfx.h"

enum { CARD_ICON, CARD_INITIALS, CARD_AVATAR };

#define CARD_Y (-0.14f)  // center of the illustration, screen units

void card_set_avatar(const uint16_t *px);  // AVATAR_SIZE*AVATAR_SIZE RGB565 pixels
void card_set_initials(const char *initials);
// t: seconds since the card appeared, left: seconds until it goes.
void card_update(int kind, int icon, float t, float left);
void card_draw(rgb_t *row, int j);
