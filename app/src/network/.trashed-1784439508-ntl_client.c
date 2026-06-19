#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif
#include "ntl_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user.h"

/* ── Cross-platform rewrite note ──────────────────────────────────────────
   The upstream NTL client (slither_vlither_ntl_spectre) talks to
   ntl-slither.com using WinHTTP on a dedicated Win32 thread, and is a no-op
   on every other platform (including the existing Linux/Android builds).
   This version uses mongoose — already vendored in this project and already
   proven to work on Android for the wss:// game connection — so the same
   request/response logic now runs identically on Windows, Linux and
   Android. Everything below runs on the main thread once per frame via
   ntl_client_poll(), so the original CRITICAL_SECTION locking is gone too:
   there is only ever one thread touching this state. */

#define NTL_HOST "ntl-slither.com"
#define NTL_POLL_INTERVAL 3.0 /* seconds between sync requests          */
#define NTL_RESPONSE_BUF_SIZE 32768

#define MAX_PENDING_CHAT_MESSAGES 16
#define MAX_OUTGOING_CHAT_MESSAGES 16

typedef struct pending_chat {
  char sender[32];
  char text[128];
} pending_chat;

typedef struct ntl_msg_cache {
  char user_id[9];
  char last_msg[128];
} ntl_msg_cache;

static struct mg_mgr ntl_mgr;
static bool ntl_running = false;
static double next_poll_time = 0;
static char pending_request_line[2048];

static ntl_player ntl_players_list[MAX_NTL_PLAYERS];
static int ntl_player_count = 0;

static pending_chat pending_chat_queue[MAX_PENDING_CHAT_MESSAGES];
static int pending_chat_count = 0;

static char outgoing_chat_queue[MAX_OUTGOING_CHAT_MESSAGES][128];
static int outgoing_chat_count = 0;

static ntl_msg_cache msg_caches[MAX_NTL_PLAYERS];
static int msg_cache_count = 0;

static char ntl_response_buf[NTL_RESPONSE_BUF_SIZE];

static const char* clean_nick(const char* raw_nick) {
  if (!raw_nick) return "";
  if (strlen(raw_nick) >= 8) {
    bool is_hex = true;
    for (int i = 0; i < 8; i++) {
      char c = raw_nick[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'))) {
        is_hex = false;
        break;
      }
    }
    if (is_hex) return raw_nick + 8;
  }
  return raw_nick;
}

static void url_decode(const char* src, char* dest, int max_len) {
  int d_idx = 0;
  for (int i = 0; src[i] && d_idx < max_len - 1;) {
    if (src[i] == '%') {
      if (src[i + 1] && src[i + 2]) {
        char hex[3] = {src[i + 1], src[i + 2], '\0'};
        dest[d_idx++] = (char)strtol(hex, NULL, 16);
        i += 3;
      } else {
        dest[d_idx++] = src[i++];
      }
    } else if (src[i] == '+') {
      dest[d_idx++] = ' ';
      i++;
    } else {
      dest[d_idx++] = src[i++];
    }
  }
  dest[d_idx] = '\0';
}

static void decode_html_entities(const char* src, char* dest, int max_len) {
  int d_idx = 0;
  for (int i = 0; src[i] && d_idx < max_len - 1;) {
    if (src[i] == '&') {
      if (strncmp(src + i, "&nbsp;", 6) == 0) {
        dest[d_idx++] = ' ';
        i += 6;
      } else if (strncmp(src + i, "&amp;", 5) == 0) {
        dest[d_idx++] = '&';
        i += 5;
      } else if (strncmp(src + i, "&lt;", 4) == 0) {
        dest[d_idx++] = '<';
        i += 4;
      } else if (strncmp(src + i, "&gt;", 4) == 0) {
        dest[d_idx++] = '>';
        i += 4;
      } else if (strncmp(src + i, "&quot;", 6) == 0) {
        dest[d_idx++] = '"';
        i += 6;
      } else if (strncmp(src + i, "&#39;", 5) == 0) {
        dest[d_idx++] = '\'';
        i += 5;
      } else if (strncmp(src + i, "&#039;", 6) == 0) {
        dest[d_idx++] = '\'';
        i += 6;
      } else {
        dest[d_idx++] = src[i++];
      }
    } else {
      dest[d_idx++] = src[i++];
    }
  }
  dest[d_idx] = '\0';
}

static void url_encode(const char* src, char* dest, int max_len) {
  int d_idx = 0;
  for (int i = 0; src[i] && d_idx < max_len - 3; i++) {
    char c = src[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      dest[d_idx++] = c;
    } else {
      sprintf(dest + d_idx, "%%%02X", (unsigned char)c);
      d_idx += 3;
    }
  }
  dest[d_idx] = '\0';
}

static void extract_json_value(const char* obj, const char* key, char* dest,
                                int max_len) {
  dest[0] = '\0';
  char search_key[64];
  sprintf(search_key, "\"%s\":", key);
  const char* p = strstr(obj, search_key);
  if (!p) return;
  p += strlen(search_key);
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '"') {
    p++;
    int i = 0;
    while (*p && *p != '"' && i < max_len) {
      dest[i++] = *p++;
    }
    dest[i] = '\0';
  } else {
    int i = 0;
    while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\t' &&
           i < max_len) {
      dest[i++] = *p++;
    }
    dest[i] = '\0';
  }
}

void ntl_client_send_msg(const char* text) {
  if (outgoing_chat_count < MAX_OUTGOING_CHAT_MESSAGES) {
    strncpy(outgoing_chat_queue[outgoing_chat_count], text, 127);
    outgoing_chat_queue[outgoing_chat_count][127] = '\0';
    outgoing_chat_count++;
  }
}

bool ntl_client_poll_chat(char* sender, char* text) {
  if (pending_chat_count <= 0) return false;
  strcpy(sender, pending_chat_queue[0].sender);
  strcpy(text, pending_chat_queue[0].text);
  for (int i = 1; i < pending_chat_count; i++) {
    pending_chat_queue[i - 1] = pending_chat_queue[i];
  }
  pending_chat_count--;
  return true;
}

static void ntl_parse_response(const char* body, const char* my_nick_with_id) {
  ntl_player temp_players[MAX_NTL_PLAYERS];
  int temp_count = 0;

  const char* p = body;
  while ((p = strstr(p, "{")) && temp_count < MAX_NTL_PLAYERS) {
    const char* end = strstr(p, "}");
    if (!end) break;

    int obj_len = (int)(end - p) + 1;
    char* obj = malloc(obj_len + 1);
    if (!obj) break;
    memcpy(obj, p, obj_len);
    obj[obj_len] = '\0';

    ntl_player* pl = &temp_players[temp_count];
    extract_json_value(obj, "nick", pl->nickname, 31);
    extract_json_value(obj, "score", pl->score, 15);
    extract_json_value(obj, "srv", pl->server, 31);
    extract_json_value(obj, "valx", pl->valx, 15);
    extract_json_value(obj, "valy", pl->valy, 15);

    char bot_val[16] = {0};
    extract_json_value(obj, "bot", bot_val, 15);
    pl->is_bot = (strcmp(bot_val, "true") == 0);

    char sos_val[16] = {0};
    extract_json_value(obj, "sos", sos_val, 15);
    pl->is_sos = (strcmp(sos_val, "true") == 0);

    extract_json_value(obj, "ver", pl->ver, 15);
    extract_json_value(obj, "owner", pl->tlm, 31);
    extract_json_value(obj, "dt", pl->dt, 127);
    extract_json_value(obj, "sid", pl->sid, 15);

    char pl_msg[128] = {0};
    extract_json_value(obj, "msg", pl_msg, 127);

    char decoded_msg[128] = {0};
    if (pl_msg[0] != '\0') {
      char raw_decoded[128] = {0};
      url_decode(pl_msg, raw_decoded, sizeof(raw_decoded));
      decode_html_entities(raw_decoded, decoded_msg, sizeof(decoded_msg));
    }

    if (strcmp(pl->nickname, my_nick_with_id) != 0) {
      char pl_id[9] = {0};
      if (strlen(pl->nickname) >= 8) {
        strncpy(pl_id, pl->nickname, 8);
        pl_id[8] = '\0';
      }

      if (pl_id[0] != '\0') {
        int cached_idx = -1;
        for (int c = 0; c < msg_cache_count; c++) {
          if (strcmp(msg_caches[c].user_id, pl_id) == 0) {
            cached_idx = c;
            break;
          }
        }
        if (decoded_msg[0] != '\0') {
          if (cached_idx != -1) {
            if (strcmp(msg_caches[cached_idx].last_msg, decoded_msg) != 0) {
              strcpy(msg_caches[cached_idx].last_msg, decoded_msg);
              if (pending_chat_count < MAX_PENDING_CHAT_MESSAGES) {
                strcpy(pending_chat_queue[pending_chat_count].sender,
                       clean_nick(pl->nickname));
                strcpy(pending_chat_queue[pending_chat_count].text,
                       decoded_msg);
                pending_chat_count++;
              }
            }
          } else if (msg_cache_count < MAX_NTL_PLAYERS) {
            int new_cache_idx = msg_cache_count++;
            strcpy(msg_caches[new_cache_idx].user_id, pl_id);
            strcpy(msg_caches[new_cache_idx].last_msg, decoded_msg);
            if (pending_chat_count < MAX_PENDING_CHAT_MESSAGES) {
              strcpy(pending_chat_queue[pending_chat_count].sender,
                     clean_nick(pl->nickname));
              strcpy(pending_chat_queue[pending_chat_count].text,
                     decoded_msg);
              pending_chat_count++;
            }
          }
        } else if (cached_idx != -1) {
          msg_caches[cached_idx].last_msg[0] = '\0';
        }
      }
      temp_count++;
    }

    free(obj);
    p = end + 1;
  }

  ntl_player_count = temp_count;
  memcpy(ntl_players_list, temp_players, sizeof(ntl_player) * temp_count);
}

static void ntl_event_handler(struct mg_connection* c, int ev, void* ev_data) {
  if (ev == MG_EV_CONNECT) {
    mg_printf(c, "%s", pending_request_line);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;
    size_t len = hm->body.len;
    if (len >= NTL_RESPONSE_BUF_SIZE) len = NTL_RESPONSE_BUF_SIZE - 1;
    memcpy(ntl_response_buf, hm->body.buf, len);
    ntl_response_buf[len] = '\0';

    if (len > 0) {
      /* my_nick_with_id was embedded in the request URL just before this
         connection was opened — re-derive it the same way it was built. */
      ntl_parse_response(ntl_response_buf, (const char*)c->fn_data);
    } else {
      printf("[NTL sync] Error: empty response body received from server.\n");
    }
    free(c->fn_data);
    c->fn_data = NULL;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR) {
    printf("[NTL sync] connection error: %s\n", (char*)ev_data);
    if (c->fn_data) {
      free(c->fn_data);
      c->fn_data = NULL;
    }
  } else if (ev == MG_EV_CLOSE) {
    /* Covers the rare case where the socket closes (timeout, reset, etc.)
       without ever reaching MG_EV_HTTP_MSG or MG_EV_ERROR above. */
    if (c->fn_data) {
      free(c->fn_data);
      c->fn_data = NULL;
    }
  }
}

static void ntl_issue_request(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;

  char active_tid[33] = {0};
  char active_auth[65] = {0};
  if (usrs->ntl_active_team_idx >= 0 &&
      usrs->ntl_active_team_idx < usrs->ntl_team_count) {
    strcpy(active_tid, usrs->ntl_teams[usrs->ntl_active_team_idx].team_id);
    strcpy(active_auth, usrs->ntl_teams[usrs->ntl_active_team_idx].auth_key);
  }

  if (active_auth[0] == '\0' || active_tid[0] == '\0') {
    /* No team configured yet — nothing to sync until the player sets one
       up in Settings. */
    return;
  }

  if (usrs->ntl_user_id[0] == '\0') {
    for (int i = 0; i < 8; i++) {
      sprintf(usrs->ntl_user_id + i, "%x", rand() % 16);
    }
    usrs->ntl_user_id[8] = '\0';
    printf("[NTL sync] Generated new random client ID: %s\n",
           usrs->ntl_user_id);
    save_user_settings(usrs);
  }

  char nick_with_id[128];
  sprintf(nick_with_id, "%s%s", usrs->ntl_user_id, usrs->nickname);

  char enc_nick[256] = {0};
  char enc_srv[128] = {0};
  char enc_dt[256] = {0};

  url_encode(nick_with_id, enc_nick, sizeof(enc_nick));

  if (gdata->conn == CONNECTED && gdata->curr_screen == PLAYING) {
    url_encode(usrs->ipv4, enc_srv, sizeof(enc_srv));
  } else {
    strcpy(enc_srv, "_GAME_MENU_");
  }

  char raw_dt[256];
  sprintf(raw_dt, "FPS: %d @ %d(%d) ms @ %d K/m", gdata->data.fps,
          gdata->data.ping, gdata->data.ping, gdata->data.kills);
  url_encode(raw_dt, enc_dt, sizeof(enc_dt));

  float valx = 0;
  float valy = 0;
  int snakes_len = tdarray_length(gdata->data.snakes);
  if (snakes_len > 0) {
    snake* me = gdata->data.snakes + (snakes_len - 1);
    valx = me->xx + me->fx;
    valy = me->yy + me->fy;
  } else {
    valx = gdata->data.view_xx;
    valy = gdata->data.view_yy;
  }

  char raw_msg[512] = {0};
  if (outgoing_chat_count > 0) {
    int len = 0;
    for (int i = 0; i < outgoing_chat_count; i++) {
      if (i > 0 && len + 3 < (int)sizeof(raw_msg)) {
        strcat(raw_msg, " | ");
        len += 3;
      }
      if (len + (int)strlen(outgoing_chat_queue[i]) < (int)sizeof(raw_msg)) {
        strcat(raw_msg, outgoing_chat_queue[i]);
        len += (int)strlen(outgoing_chat_queue[i]);
      }
    }
    outgoing_chat_count = 0;
  }

  char enc_msg[1024] = {0};
  if (raw_msg[0] != '\0') {
    url_encode(raw_msg, enc_msg, sizeof(enc_msg));
  }

  int my_snake_id =
      (gdata->conn == CONNECTED && gdata->curr_screen == PLAYING)
          ? gdata->data.snake_id
          : -1;

  char path[2048];
  sprintf(path,
          "/slither/ntlplay-mt.php?auth=%s&tid=%s&nick=%s&score=%d&valx=%d&"
          "valy=%d&bot=%s&sos=false&food=false&srv=%s&msg=%s&dt=%s&i=%d&"
          "ver=9.19&di=1000",
          active_auth, active_tid, enc_nick, gdata->data.score, (int)valx,
          (int)valy, usrs->hotkeys[HOTKEY_BOT].active ? "true" : "false",
          enc_srv, enc_msg, enc_dt, my_snake_id);

  sprintf(pending_request_line,
          "GET %s HTTP/1.1\r\nHost: " NTL_HOST
          "\r\nUser-Agent: Vlither NTL/1.0\r\nConnection: close\r\n\r\n",
          path);

  char full_url[2100];
  sprintf(full_url, "https://" NTL_HOST "%s", path);

  /* fn_data carries the nick-with-id this request was sent under, so the
     response handler can tell "me" apart from teammates. Freed in the
     MG_EV_HTTP_MSG / MG_EV_ERROR handlers above. */
  char* nick_copy = malloc(strlen(nick_with_id) + 1);
  if (!nick_copy) return;
  strcpy(nick_copy, nick_with_id);

  if (!mg_http_connect(&ntl_mgr, full_url, ntl_event_handler, nick_copy)) {
    printf("[NTL sync] failed to open connection to %s\n", NTL_HOST);
    free(nick_copy);
  }
}

void ntl_client_start(tenv* env) {
  if (ntl_running) return;
  mg_mgr_init(&ntl_mgr);
  ntl_running = true;
  ntl_player_count = 0;
  next_poll_time = 0; /* fire the first sync immediately */
}

void ntl_client_stop(void) {
  if (!ntl_running) return;
  ntl_running = false;
  mg_mgr_free(&ntl_mgr);
}

void ntl_client_poll(tenv* env) {
  if (!ntl_running) return;
  mg_mgr_poll(&ntl_mgr, 0);

  double now = glfwGetTime();
  if (now >= next_poll_time) {
    next_poll_time = now + NTL_POLL_INTERVAL;
    ntl_issue_request(env);
  }
}

ntl_player* ntl_get_players(int* count) {
  if (!ntl_running) {
    *count = 0;
    return NULL;
  }
  *count = ntl_player_count;
  return ntl_players_list;
}
