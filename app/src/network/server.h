#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <stddef.h>

#include <thermite.h>

#include "../game/user_settings.h"

void server_init(tenv* env);
void server_connect(tenv* env);
void server_poll(tenv* env);
void server_destroy(tenv* env);

void server_list_init(tenv* env);
void server_list_fetch(tenv* env);
void server_list_poll(tenv* env);
void server_list_destroy(tenv* env);
void server_list_start_ping(tenv* env);
bool server_list_resolve_sid(tenv* env, int sid, char* address,
                             size_t address_size);

#define CUSTOM_SERVER_COUNT 10
extern const char* CUSTOM_SERVER_NAMES[CUSTOM_SERVER_COUNT];

#define BD_ID_OPCODE 67

bool server_is_bd_id_target(const char* server_address);
bool server_is_custom_server(tenv* env, const char* address);

bool server_bd_id_should_send(const char* server_address);

bool server_bd_id_handle_command(tenv* env, const char* input);

#endif
