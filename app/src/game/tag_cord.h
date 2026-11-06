#ifndef TAG_CORD_H
#define TAG_CORD_H

#include <stdbool.h>

#include "../user.h"  // cimgui types (ImDrawList, ImVec2)

// Shared tag "antenna" (the string the tag hangs on). Ported from vlither-main.
// The cord follows a dense, smooth trail laid down along the head's own path,
// so it moves exactly as smooth as the worm. Used by NTL tags, Vlither tags and
// the skin-editor preview so every tag cord looks identical.

#define TAG_CORD_MAX_POINTS 140

typedef struct tag_cord {
  ImVec2 pls[TAG_CORD_MAX_POINTS];  // screen-space polyline, neck -> tag end
  int np;
  float ang;   // eased cord direction (head -> tag end), radians, screen space
  float wide;  // base string thickness (px)
} tag_cord;

typedef struct tag_cord_view {
  float mww2, mhh2;      // viewport half size
  float view_xx, view_yy;  // camera world position
  float gsc;             // world -> screen zoom
} tag_cord_view;

// Build the cord for a snake. hx/hy = head in WORLD coords, rad = body radius in
// WORLD units, head_ang = snake heading. slot (0/1) keeps separate angle easing
// when one snake shows two tags. fan_px bends the far end sideways (screen px)
// so a second tag on the same snake does not sit on top of the first.
// Always yields at least a valid straight cord.
void tag_cord_build(tag_cord *c, int snake_id, int slot, float hx, float hy,
                    float rad, float head_ang, const tag_cord_view *v,
                    float fan_px);

// Build a straight cord in SCREEN space (skin-editor preview, no body trail).
void tag_cord_build_straight(tag_cord *c, float head_x, float head_y,
                             float rad_px, float head_ang);

// Draw the cord. main_rgb = core colour, rim_rgb = outer colour (0..1).
void tag_cord_draw(ImDrawList *dl, const tag_cord *c, const float main_rgb[3],
                   const float rim_rgb[3], float alpha);

#endif
