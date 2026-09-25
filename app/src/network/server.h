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

/* Opcode for the Battledome client-ID message, per noaha's v9.68 spec. */
#define BD_ID_OPCODE 67

/* True if `server_address` (as stored in user_settings, e.g. "1.2.3.4:444")
   is one of the Battledome servers that should receive the persistent
   client-ID message right after the riddle answer. Intentionally separate
   from CUSTOM_SERVER_IPS/CUSTOM_SERVER_NAMES (the visible server picker) —
   this only gates the ID handshake. Also exposed standalone for !idlist. */
bool server_is_bd_id_target(const char* server_address);

/* server_is_bd_id_target(server_address), OR'd with the !idforce dev
   override. This is what callback.c should actually check before sending
   the ID packet. */
bool server_bd_id_should_send(const char* server_address);

/* Handles !id, !idlist and !idforce typed into chat. Returns true (and
   prints a local system message) if `input` was one of these commands, so
   the caller knows not to send it to the server as a real chat message. */
bool server_bd_id_handle_command(tenv* env, const char* input);

#endif
