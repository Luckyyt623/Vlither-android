#ifndef TRANSPORT_H
#define TRANSPORT_H

/* Two ways to talk to a Slither server:

   CLIENT_WEB     wss://host/slither, one websocket message per packet (the
                  normal Vlither path).
   CLIENT_MOBILE  raw TCP like the Android app: ip:(port + 10), every packet
                  prefixed with its length (4 bytes until the server's first
                  'a' packet, 2 bytes after), and the Android riddle answer
                  (CRC32 based) instead of the web one.

   The choice is made on the homepage and saved in client_mode.dat. A
   connection knows whether it is raw by itself, so net_send() can be used
   for any game connection. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <thermite.h>

#include "../external/mongoose.h"

enum { CLIENT_WEB = 0, CLIENT_MOBILE = 1 };

int net_client_mode(void);
void net_set_client_mode(int mode); /* also saves it */

/* Sends one game packet on conn: a websocket binary message, or a length
   prefixed frame on a raw connection. */
void net_send(struct mg_connection* c, const void* buf, size_t len);

bool net_is_raw(const struct mg_connection* c);

/* Opens the raw TCP connection for "ip:port" (the port is the web port from
   the server list; the Android client adds 10). NULL on failure. */
struct mg_connection* net_connect_raw(struct mg_mgr* mgr, const char* address,
                                      mg_event_handler_t fn, void* fn_data);

/* Event hooks, called from server_callback for a raw connection. */
void net_raw_connected(tenv* env, struct mg_connection* c);
void net_raw_read(tenv* env, struct mg_connection* c);

/* Called when the server's 'a' (init) packet arrives: switches the length
   prefix of a raw connection to 2 bytes, like the Android client. */
void net_raw_on_init(struct mg_connection* c);

#endif
