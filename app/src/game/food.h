#ifndef FOOD_H
#define FOOD_H

#include <stdint.h>
#include <stdbool.h>

typedef enum food_origin {
  FOOD_ORIGIN_NORMAL = 0,
  FOOD_ORIGIN_DEATH = 1,
  FOOD_ORIGIN_BOOST = 2,
} food_origin;

typedef struct food {
  int id;
  int cv;
  int cv2;
  int ebid;
  int sx;
  int sy;

  float xx;
  float yy;
  float rx;
  float ry;
  float rsp;
  float rad;
  float sz;
  float lrrad;
  float fr;
  float gfr;
  float gr;
  float wsp;
  float eaten_fr;

  int origin;
  bool eaten;
} food;

#endif
