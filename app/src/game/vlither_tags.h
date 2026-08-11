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

bool vlither_chat_connected(void);
bool vlither_chat_send_text(const char *text);
int vlither_chat_history_count(void);
const char *vlither_chat_history_nick(int index);
const char *vlither_chat_history_text(int index);
const char *vlither_chat_history_server(int index);
int vlither_chat_player_count(void);
const char *vlither_chat_player_nick(int index);
const char *vlither_chat_player_server(int index);
const char *vlither_chat_player_version(int index);
const char *vlither_chat_player_client_id(int index);
int vlither_chat_player_snake_id(int index);
float vlither_chat_player_x(int index);
float vlither_chat_player_y(int index);
int vlither_chat_player_fps(int index);
int vlither_chat_player_ping(int index);
bool vlither_chat_is_snake_player(int snake_id, const char *server);

#endif
