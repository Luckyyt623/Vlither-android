#ifndef TAG_FOLLOW_H
#define TAG_FOLLOW_H

#include <math.h>
#include <stdbool.h>

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

static inline void tag_follow_point(float *px, float *py, float *vx,
                                    float *vy, float *last_mtm, bool *ready,
                                    float target_x, float target_y,
                                    float now_mtm, float stiffness,
                                    float damping) {
  float dt = (now_mtm - *last_mtm) * 0.001f;
  if (!*ready || dt < 0.0f || dt > 0.25f) {
    *px = target_x;
    *py = target_y;
    *vx = 0.0f;
    *vy = 0.0f;
    *last_mtm = now_mtm;
    *ready = true;
    return;
  }
  if (dt == 0.0f) return;
  if (dt > 0.05f) dt = 0.05f;

  *vx += (target_x - *px) * stiffness * dt;
  *vy += (target_y - *py) * stiffness * dt;
  float damp = expf(-damping * dt);
  *vx *= damp;
  *vy *= damp;
  *px += *vx * dt;
  *py += *vy * dt;
  *last_mtm = now_mtm;
}

#endif
