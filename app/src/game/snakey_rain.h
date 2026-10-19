#ifndef SNAKEY_RAIN_H
#define SNAKEY_RAIN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <thermite.h>

void snakey_rain_init(tenv *env);
void snakey_rain_destroy(tenv *env);
void snakey_rain_update(tenv *env);
void snakey_rain_draw(tenv *env);
void snakey_rain_on_spawn(tenv *env, const uint8_t *packet,
                          size_t packet_len);

bool snakey_rain_enabled_at_start(void);
bool snakey_rain_requires_restart(tenv *env);
void snakey_rain_apply_settings(tenv *env);

#endif
