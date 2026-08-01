#ifndef VLITHER_TAGS_H
#define VLITHER_TAGS_H

#include <stdbool.h>
#include <thermite.h>

#include "snake.h"

void vlither_tags_init(tenv *env);
void vlither_tags_update(tenv *env);
void vlither_tags_destroy(tenv *env);

bool vlither_tags_handle_command(const char *input);
void vlither_tags_skin_panel(tenv *env);
void vlither_tags_draw(tenv *env, snake *o, float alpha,
                       float viewport_half_w, float viewport_half_h);

#endif
