#ifndef SKIN_WHEEL_H
#define SKIN_WHEEL_H

#include <stdbool.h>
#include <thermite.h>

#include "../game/rgb_skin.h"

void sk_wheel_panel(tenv* env, float panel_w);

#define SK_WHEEL_MAX RGB_SKIN_MAX
int sk_wheel_count(void);
void sk_wheel_clear(void);
void sk_wheel_undo(void);

void sk_wheel_draw_body(tenv* env, int n, const float* px, const float* py,
                        float size, float alpha);

void sk_wheel_draw_accessory(tenv* env, float u, float v, float w, float h,
                             float cx, float cy, float half, float ang,
                             float alpha);

bool sk_wheel_active(void);
void sk_wheel_set_active(bool on);
int sk_wheel_export(rgb_seg* out, int max);
void sk_wheel_save(void);

const unsigned char* sk_wheel_seg_png(unsigned int* len);

void sk_wheel_destroy(tenv* env);

#endif
