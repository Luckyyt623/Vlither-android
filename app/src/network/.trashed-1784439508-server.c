#include "server.h"

#include "../user.h"
#include "callback.h"

void server_init(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  mg_log_set(MG_LL_NONE);
  mg_mgr_init(&gdata->network_manager);
}

void server_connect(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  char url[256] = {};
  /* Use wss:// for public servers, ws:// for localhost */
  bool is_local = (strncmp(usrs->ipv4, "127.", 4) == 0 ||
                   strncmp(usrs->ipv4, "localhost", 9) == 0 ||
                   strncmp(usrs->ipv4, "192.168.", 8) == 0 ||
                   strncmp(usrs->ipv4, "10.", 3) == 0);
  sprintf(url, "%s://%s/slither", is_local ? "ws" : "wss", usrs->ipv4);
  gdata->connection =
    mg_ws_connect(&gdata->network_manager, url, server_callback, env,
                  "%s:%s\r\n%s:%s\r\n",
                  "Origin", "https://slither.com",
                  "Host", "slither.com");
#ifdef ANDROID
  /* Pump just enough to send the HTTP GET request — game loop handles the rest */
  mg_mgr_poll(&gdata->network_manager, 5);
  mg_mgr_poll(&gdata->network_manager, 5);
#endif
}

void server_poll(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
#ifdef ANDROID
  mg_mgr_poll(&gdata->network_manager, 5);
#else
  mg_mgr_poll(&gdata->network_manager, 0);
#endif
}

void server_destroy(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;

  mg_mgr_free(&gdata->network_manager);
}