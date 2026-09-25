#ifndef VLITHER_TAGS_H
#define VLITHER_TAGS_H

#include <stdbool.h>
#include <thermite.h>

#include "snake.h"

void vlither_tags_init(tenv *env);
void vlither_tags_update(tenv *env);
void vlither_tags_destroy(tenv *env);
const char *vlither_backend_base_url(void);

bool vlither_tags_handle_command(const char *input);
void vlither_tags_skin_panel(tenv *env);
void vlither_tags_draw(tenv *env, snake *o, float alpha,
                       float viewport_half_w, float viewport_half_h);

bool vlither_chat_connected(void);
bool vlither_chat_send_text(const char *text);
/* Local-only system line in Vlither chat (not sent to server). */
void vlither_chat_system_message(const char *text);
int vlither_chat_history_count(void);
const char *vlither_chat_history_nick(int index);
const char *vlither_chat_history_text(int index);
const char *vlither_chat_history_server(int index);
const char *vlither_chat_history_color(int index);
const char *vlither_chat_history_emoji(int index);
long long vlither_chat_history_time_ms(int index);
int vlither_chat_player_count(void);
const char *vlither_chat_player_nick(int index);
const char *vlither_chat_player_server(int index);
const char *vlither_chat_player_version(int index);
const char *vlither_chat_player_client_id(int index);
const char *vlither_chat_player_map_name(int index);
const char *vlither_chat_player_color(int index);
const char *vlither_chat_player_emoji(int index);
int vlither_chat_player_snake_id(int index);
float vlither_chat_player_x(int index);
float vlither_chat_player_y(int index);
int vlither_chat_player_fps(int index);
int vlither_chat_player_ping(int index);
bool vlither_chat_player_sos(int index);
bool vlither_chat_is_snake_player(int snake_id, const char *server);
bool vlither_chat_is_nickname_player(const char *nickname, const char *server);
bool vlither_chat_is_nickname_sos(const char *nickname, const char *server);
void vlither_chat_set_joined(bool joined);
bool vlither_chat_joined(void);
void vlither_chat_set_sos_until(long long until_ms);
int vlither_profile_emoji_count(void);
const char *vlither_profile_emoji_at(int index);
void vlither_chat_profile_changed(void);

/* Event ping system. Backend timestamps are UTC epoch milliseconds; the UI
   formats them with the phone's local timezone. Interest is persisted by the
   backend using the same anonymous Vlither client ID used by Tags/Chat. */
int vlither_event_count(void);
const char *vlither_event_id_at(int index);
const char *vlither_event_name_at(int index);
const char *vlither_event_country_at(int index);
const char *vlither_event_prize_at(int index);
const char *vlither_event_rules_at(int index);
const char *vlither_event_server_at(int index);
long long vlither_event_start_at_ms(int index);
long long vlither_event_end_at_ms(int index);
long long vlither_event_remove_at_ms(int index);
bool vlither_event_interested_at(int index);
int vlither_event_interested_count_at(int index);
const char *vlither_event_status(void);
int vlither_event_next_interested(void);
bool vlither_event_set_interested(int index, bool interested);
bool vlither_event_refresh(void);

#endif
