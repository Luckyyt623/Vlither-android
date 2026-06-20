#ifndef NTL_CLIENT_H
#define NTL_CLIENT_H

#include <thermite.h>
#include <stdbool.h>

#define MAX_NTL_PLAYERS 64

typedef struct ntl_player {
  char nickname[32];
  char score[16];
  char server[32];
  char valx[16];
  char valy[16];
  bool is_bot;
  bool is_sos;
  char ver[16];
  char tlm[32];
  char dt[128];
  char sid[16];
} ntl_player;

void ntl_client_start(tenv* env);
void ntl_client_stop(void);
/* Drives the mongoose event loop and (every few seconds) sends a fresh
   sync request. Cross-platform replacement for the old background-thread
   design — must be called once per frame from the main thread (e.g. from
   tinput()) on every screen, not just while PLAYING, so teammates still
   see you as online while you're on the title/settings screen. */
void ntl_client_poll(tenv* env);
ntl_player* ntl_get_players(int* count);
void ntl_client_send_msg(const char* text);
bool ntl_client_poll_chat(char* sender, char* text);

#endif

const char** ntl_get_log_lines(int* count);
