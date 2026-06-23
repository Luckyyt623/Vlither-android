#ifndef NTL_CLIENT_H
#define NTL_CLIENT_H

#include <thermite.h>
#include <stdbool.h>

#define MAX_NTL_PLAYERS  64
#define NTL_LOG_LINES    32
#define NTL_LOG_LINE_LEN 128

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
void ntl_client_poll(tenv* env);
ntl_player* ntl_get_players(int* count);
void ntl_client_send_msg(const char* text);
bool ntl_client_poll_chat(char* sender, char* text);
const char** ntl_get_log_lines(int* count);

#endif
