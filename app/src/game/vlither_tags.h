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
bool vlither_chat_player_voice_enabled(int index);
bool vlither_chat_player_voice_muted(int index);
bool vlither_chat_player_voice_deafened(int index);
const char *vlither_chat_player_voice_room_id(int index);
bool vlither_chat_is_snake_player(int snake_id, const char *server);

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

/* Vlither Voice v1. Rooms are public by default; a non-empty password makes
   the room private. With no room selected, same-server players use proximity
   voice and the backend attenuates volume by in-game distance. Voice uses an
   open mic while enabled; mute and deafen are explicit persistent states. */
bool vlither_voice_enabled(void);
bool vlither_voice_in_room(void);
bool vlither_voice_room_host(void);
bool vlither_voice_muted(void);
bool vlither_voice_deafened(void);
const char *vlither_voice_room_id(void);
const char *vlither_voice_room_name(void);
const char *vlither_voice_status(void);
int vlither_voice_room_count(void);
const char *vlither_voice_room_id_at(int index);
const char *vlither_voice_room_name_at(int index);
bool vlither_voice_room_locked_at(int index);
int vlither_voice_room_members_at(int index);
int vlither_voice_room_max_at(int index);
int vlither_voice_room_member_count_at(int room_index);
const char *vlither_voice_room_member_name_at(int room_index,
                                              int member_index);
bool vlither_voice_room_member_host_at(int room_index, int member_index);
bool vlither_voice_room_member_muted_at(int room_index, int member_index);
bool vlither_voice_room_member_deafened_at(int room_index, int member_index);
int vlither_voice_member_count(void);
const char *vlither_voice_member_name_at(int index);
bool vlither_voice_member_host_at(int index);
bool vlither_voice_member_muted_at(int index);
bool vlither_voice_member_deafened_at(int index);
unsigned long long vlither_voice_tx_frames(void);
unsigned long long vlither_voice_rx_frames(void);
int vlither_voice_listener_count(void);
int vlither_voice_audio_state(void);
bool vlither_voice_set_enabled(bool enabled);
bool vlither_voice_refresh_rooms(void);
bool vlither_voice_create_room(const char *name, const char *password);
bool vlither_voice_join_room(const char *room_id, const char *password);
bool vlither_voice_leave_room(void);
bool vlither_voice_set_muted(bool muted);
bool vlither_voice_set_deafened(bool deafened);

#endif
