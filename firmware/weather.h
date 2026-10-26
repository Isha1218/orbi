// Weather scenes: the ball becomes a little window into the weather, no eyes.
#pragma once
#include "gfx.h"

void weather_update(int scene, float t, float left);
int weather_covers(void);  // 1 when the weather fills the whole ball
void weather_draw(rgb_t *row, int j);
