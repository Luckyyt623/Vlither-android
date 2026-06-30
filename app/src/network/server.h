#ifndef SERVER_H
#define SERVER_H

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

#endif
