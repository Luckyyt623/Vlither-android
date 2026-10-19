#ifndef RGB_SKIN_H
#define RGB_SKIN_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define RGB_SKIN_MAX 66
#define RGB_SKIN_SHAPES 6
#define RGB_SEG_PALETTE 255

typedef struct rgb_seg {
  uint8_t shape;
  uint8_t r, g, b;
} rgb_seg;

static inline int rgb_skin_type_of_shape(int shape) {
  return shape >= 2 ? shape : 1;
}

static inline bool rgb_skin_shape_ok(int shape, int type) {
  if (shape < 0 || shape >= RGB_SKIN_SHAPES) return false;
  return type == 1 ? shape <= 1 : shape == type;
}

static inline int rgb_skin_first_shape(int type) {
  return type == 1 ? 0 : type;
}

static inline bool rgb_skin_palette_ok(int cv) {
  return (cv >= 0 && cv <= 35) || cv == 37 || cv == 39 || cv == 41;
}

static inline void rgb_skin_fix_color(rgb_seg* s) {
  static const int min2c[RGB_SKIN_SHAPES] = {255, 255, 128, 128, 128, 128};
  if (s->shape >= RGB_SKIN_SHAPES) return;
  int r = s->r, g = s->g, b = s->b;
  int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  int mid = r + g + b - mx - mn;
  int need = min2c[s->shape];
  if (mid + mx < need) {
    int j = 1 + (need - (mid + mx)) / 2;
    r += j;
    g += j;
    b += j;
    s->r = (uint8_t)(r > 255 ? 255 : r);
    s->g = (uint8_t)(g > 255 ? 255 : g);
    s->b = (uint8_t)(b > 255 ? 255 : b);
  }
}

#endif
