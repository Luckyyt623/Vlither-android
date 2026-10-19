#ifndef NTL_TAGS_H
#define NTL_TAGS_H

#include <stdbool.h>
#include <thermite.h>

#include "snake.h"

void ntl_tags_init(tenv *env);
void ntl_tags_update(tenv *env);
void ntl_tags_destroy(tenv *env);

bool ntl_tags_handle_command(const char *input);

bool ntl_tags_select_public(int id);
bool ntl_tags_request_private(int id, const char *password);
void ntl_tags_disable(void);
void ntl_tags_skin_panel(tenv *env);
int ntl_tags_preview_id(void);
void ntl_tags_draw_preview(tenv *env, int id, float head_x, float head_y,
                           float angle, float preview_snake_scale);

bool ntl_tags_is_packet_tag(int id);
bool ntl_tags_is_protected_tag(int id);
bool ntl_tags_exists(int id);

void ntl_tags_begin_frame(void);
void ntl_tags_draw(tenv *env, snake *o, float alpha,
                   float viewport_half_w, float viewport_half_h);

#endif
