#ifndef ARROW_STYLES_H
#define ARROW_STYLES_H

#include "constants.h"

static inline void arrow_style_uv_bounds(int style, float* u0, float* v0,
                                         float* u1, float* v1) {
  if (style < 0 || style >= ARROW_STYLE_COUNT) style = 0;
  int column = style % 3;
  int row = style / 3;
  *u0 = (column * 256.0f + 8.0f) / 768.0f;
  *v0 = (row * 256.0f + 8.0f) / 768.0f;
  *u1 = (column * 256.0f + 248.0f) / 768.0f;
  *v1 = (row * 256.0f + 248.0f) / 768.0f;
}

static inline float arrow_style_aspect(int style) {
  return style == 0 ? 1.5f : 1.0f;
}

#endif
