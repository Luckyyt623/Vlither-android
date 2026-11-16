#ifndef TAG_CORD_H
#define TAG_CORD_H

#include <stdbool.h>

#include "../user.h"

#define TAG_CORD_MAX_POINTS 140

typedef struct tag_cord {
  ImVec2 pls[TAG_CORD_MAX_POINTS];
  int np;
  float ang;
  float wide;
} tag_cord;

typedef struct tag_cord_view {
  float mww2, mhh2;
  float view_xx, view_yy;
  float gsc;
} tag_cord_view;

void tag_cord_build(tag_cord *c, int snake_id, int slot, float hx, float hy,
                    float rad, float head_ang, const tag_cord_view *v,
                    float fan_px);

void tag_cord_build_straight(tag_cord *c, float head_x, float head_y,
                             float rad_px, float head_ang);

void tag_cord_draw(ImDrawList *dl, const tag_cord *c, const float main_rgb[3],
                   const float rim_rgb[3], float alpha);

#endif
