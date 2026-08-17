#ifndef ARROW_STYLES_H
#define ARROW_STYLES_H

#include "constants.h"

/* Nine arrows packed in a 3x3, 768px atlas. Each 256px cell has an 8px
   transparent gutter to keep neighbouring sprites out of generated mipmaps. */
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

/* redarrow.webp was supplied as 612x408; the remaining source canvases are
   square. The atlas stores every cell square and this restores its shape. */
static inline float arrow_style_aspect(int style) {
  return style == 0 ? 1.5f : 1.0f;
}

#endif
