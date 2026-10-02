#ifndef FOOD_H
#define FOOD_H

#include <stdint.h>
#include <stdbool.h>

/* Where a food pellet came from, so its on-screen size can be scaled
   independently in settings. Not persisted anywhere: food is transient game
   data rebuilt from the network stream, never saved to disk. */
typedef enum food_origin {
  FOOD_ORIGIN_NORMAL = 0, /* ambient food filling a sector ('F' batches) */
  FOOD_ORIGIN_DEATH = 1,  /* scattered when a snake dies ('f' single-adds) */
  FOOD_ORIGIN_BOOST = 2,  /* dropped behind a boosting snake ('b') */
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
