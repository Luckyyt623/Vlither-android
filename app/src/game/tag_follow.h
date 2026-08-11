#ifndef TAG_FOLLOW_H
#define TAG_FOLLOW_H

#include <math.h>
#include <stdbool.h>

/* Frame-rate-independent angular follow used by both NTL and Vlither tags.
   A short gap resets the pose so a newly visible/re-enabled tag never swings
   in from a stale direction. */
static inline float tag_follow_angle(float *angle, float *last_mtm,
                                     bool *ready, float target,
                                     float now_mtm, float response) {
  float dt = (now_mtm - *last_mtm) * 0.001f;
  if (!*ready || dt < 0.0f || dt > 0.25f) {
    *angle = target;
    *last_mtm = now_mtm;
    *ready = true;
    return target;
  }
  if (dt == 0.0f) return *angle;

  if (dt > 0.05f) dt = 0.05f;
  float delta = atan2f(sinf(target - *angle), cosf(target - *angle));
  float blend = 1.0f - expf(-response * dt);
  *angle += delta * blend;
  *angle = atan2f(sinf(*angle), cosf(*angle));
  *last_mtm = now_mtm;
  return *angle;
}

#endif
