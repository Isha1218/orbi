// Scenes. Weather fills the whole ball with no eyes; the others play around the eyes.
#pragma once
#include "gfx.h"

int scene_is_weather(int scene);
// t: seconds since the scene started, left: seconds until it ends.
void scene_update(int scene, float t, float left);
int scene_covers(void);                    // 1 when the scene hides the whole ball
void scene_draw_back(rgb_t *row, int j);   // weather, or what is behind the eyes
void scene_draw_front(rgb_t *row, int j);  // what is in front of the eyes
