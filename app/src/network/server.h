#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>

#include <thermite.h>

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

#endif
