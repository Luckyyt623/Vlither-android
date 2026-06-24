/*
 * ntl_client.c  –  NTL Teammate Sync for Vlither Android
 *
 * Runs a background pthread that periodically GETs the NTL roster from
 * https://ntl-slither.com using a dedicated Mongoose 7 HTTPS connection.
 * Player data + chat messages are exchanged with the game thread through a
 * pthread_mutex-protected shared state.
 *
 * Thread model:
 *   main thread  – reads ntl_players_list / polls chat via ntl_get_players()
 *                  and ntl_client_poll_chat(); both take ntl_lock briefly.
 *   NTL thread   – owns its own mg_mgr; writes player list + chat queue
 *                  under ntl_lock.
 */

#include "ntl_client.h"
#include "../user.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* Mongoose header is already available in the project */
#include "../external/mongoose.h"

#ifdef ANDROID
#include <android/log.h>
#define NTL_LOG(fmt,...) __android_log_print(ANDROID_LOG_DEBUG,"vlither_ntl",fmt,##__VA_ARGS__)
#else
#define NTL_LOG(fmt,...) printf("[NTL] " fmt "\n", ##__VA_ARGS__)
#endif

/* ── Shared state ────────────────────────────────────────────────────── */

static pthread_t        ntl_thread_id;
static volatile bool    ntl_running   = false;
static pthread_mutex_t  ntl_lock      = PTHREAD_MUTEX_INITIALIZER;
static bool             ntl_started   = false;

static ntl_player ntl_players_list[MAX_NTL_PLAYERS];
static int        ntl_player_count = 0;

/* Chat queues */
#define MAX_PENDING_CHAT 16
#define MAX_OUTGOING_CHAT 16

typedef struct pending_chat {
  char sender[32];
  char text[128];
} pending_chat;

static pending_chat pending_chat_queue[MAX_PENDING_CHAT];
static int          pending_chat_count = 0;

static char outgoing_chat_queue[MAX_OUTGOING_CHAT][128];
static int  outgoing_chat_count = 0;

/* Dedup cache so the same chat message is not re-delivered each poll */
typedef struct ntl_msg_cache {
  char user_id[9];
  char last_msg[128];
} ntl_msg_cache;

static ntl_msg_cache msg_caches[MAX_NTL_PLAYERS];
static int           msg_cache_count = 0;

/* ── String helpers ──────────────────────────────────────────────────── */

static const char* clean_nick(const char* raw) {
  if (!raw) return "";
  if (strlen(raw) >= 8) {
    bool hex = true;
    for (int i = 0; i < 8; i++) {
      char c = raw[i];
      if (!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) { hex=false; break; }
    }
    if (hex) return raw + 8;
  }
  return raw;
}

static void url_encode(const char* src, char* dst, int max) {
  int d = 0;
  for (int i = 0; src[i] && d < max-3; i++) {
    char c = src[i];
    if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')
      dst[d++] = c;
    else { sprintf(dst+d,"%%%02X",(unsigned char)c); d+=3; }
  }
  dst[d] = '\0';
}

static void url_decode(const char* src, char* dst, int max) {
  int d = 0;
  for (int i = 0; src[i] && d < max-1; ) {
    if (src[i]=='%' && src[i+1] && src[i+2]) {
      char hex[3]={src[i+1],src[i+2],'\0'};
      dst[d++]=(char)strtol(hex,NULL,16); i+=3;
    } else if (src[i]=='+') { dst[d++]=' '; i++; }
    else { dst[d++]=src[i++]; }
  }
  dst[d] = '\0';
}

static void decode_html(const char* src, char* dst, int max) {
  int d = 0;
  for (int i = 0; src[i] && d < max-1; ) {
    if (src[i]=='&') {
      if      (!strncmp(src+i,"&amp;" ,5)) { dst[d++]='&'; i+=5; }
      else if (!strncmp(src+i,"&lt;"  ,4)) { dst[d++]='<'; i+=4; }
      else if (!strncmp(src+i,"&gt;"  ,4)) { dst[d++]='>'; i+=4; }
      else if (!strncmp(src+i,"&quot;",6)) { dst[d++]='"'; i+=6; }
      else if (!strncmp(src+i,"&#39;" ,5)) { dst[d++]='\'';i+=5; }
      else if (!strncmp(src+i,"&nbsp;",6)) { dst[d++]=' '; i+=6; }
      else { dst[d++]=src[i++]; }
    } else { dst[d++]=src[i++]; }
  }
  dst[d] = '\0';
}

static void extract_json_value(const char* obj, const char* key, char* dst, int max) {
  dst[0] = '\0';
  char k[64]; sprintf(k,"\"%s\":",key);
  const char* p = strstr(obj,k);
  if (!p) return;
  p += strlen(k);
  while (*p==' '||*p=='\t') p++;
  if (*p=='"') {
    p++;
    int i=0;
    while (*p && *p!='"' && i<max-1) dst[i++]=*p++;
    dst[i]='\0';
  } else {
    int i=0;
    while (*p && *p!=',' && *p!='}' && *p!=' ' && i<max-1) dst[i++]=*p++;
    dst[i]='\0';
  }
}

/* ── HTTP response context passed to Mongoose callback ──────────────── */

typedef struct ntl_http_ctx {
  volatile bool done;
  char body[65536];
  size_t body_len;
  char path[2048]; /* full request path including query string */
} ntl_http_ctx;

/* ── Mongoose HTTP event handler ─────────────────────────────────────── */

static void ntl_http_cb(struct mg_connection* c, int ev, void* ev_data) {
  ntl_http_ctx* ctx = (ntl_http_ctx*)c->fn_data;

  if (ev == MG_EV_CONNECT) {
    /* TCP connected — for plain HTTP we'd send here,
     * but we're HTTPS so wait for MG_EV_TLS_HS instead. */
    (void)ev_data;
  } else if (ev == MG_EV_TLS_HS) {
    /* TLS handshake complete — safe to send HTTP request now */
    mg_printf(c,
      "GET %s HTTP/1.0\r\n"
      "Host: ntl-slither.com\r\n"
      "Connection: close\r\n"
      "\r\n",
      ctx->path);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;
    size_t len = hm->body.len;
    if (len >= sizeof(ctx->body)) len = sizeof(ctx->body)-1;
    memcpy(ctx->body, hm->body.buf, len);
    ctx->body[len] = '\0';
    ctx->body_len  = len;
    c->is_closing  = 1;
    ctx->done      = true;
  } else if (ev == MG_EV_ERROR) {
    NTL_LOG("HTTP error: %s", (char*)ev_data);
    ctx->done = true;
    c->is_closing = 1;
  } else if (ev == MG_EV_CLOSE) {
    ctx->done = true;
  }
}

/* ── JSON array parser ──────────────────────────────────────────────── */

static void parse_and_store_response(const char* body, const char* my_nick_with_id) {
  ntl_player temp[MAX_NTL_PLAYERS];
  int count = 0;

  const char* p = body;
  while ((p = strstr(p, "{")) != NULL && count < MAX_NTL_PLAYERS) {
    const char* end = strstr(p, "}");
    if (!end) break;

    int obj_len = (int)(end - p) + 1;
    char* obj = malloc(obj_len + 1);
    if (!obj) break;
    memcpy(obj, p, obj_len);
    obj[obj_len] = '\0';

    ntl_player* pl = &temp[count];
    memset(pl, 0, sizeof(*pl));

    extract_json_value(obj, "nick",  pl->nickname, 31);
    extract_json_value(obj, "score", pl->score,    15);
    extract_json_value(obj, "srv",   pl->server,   31);
    extract_json_value(obj, "valx",  pl->valx,     15);
    extract_json_value(obj, "valy",  pl->valy,     15);
    extract_json_value(obj, "ver",   pl->ver,      15);
    extract_json_value(obj, "owner", pl->tlm,      31);
    extract_json_value(obj, "dt",    pl->dt,       127);
    extract_json_value(obj, "sid",   pl->sid,      15);

    char bot_val[16]={0}; extract_json_value(obj,"bot",bot_val,15);
    pl->is_bot = (strcmp(bot_val,"true")==0);
    char sos_val[16]={0}; extract_json_value(obj,"sos",sos_val,15);
    pl->is_sos = (strcmp(sos_val,"true")==0);

    /* Chat dedup + injection */
    if (strcmp(pl->nickname, my_nick_with_id) != 0) {
      char pl_msg[128]={0};
      extract_json_value(obj,"msg",pl_msg,127);
      char decoded[128]={0};
      if (pl_msg[0]) {
        char raw[128]={0};
        url_decode(pl_msg, raw, sizeof(raw));
        decode_html(raw, decoded, sizeof(decoded));
      }

      char pl_id[9]={0};
      if (strlen(pl->nickname)>=8) { strncpy(pl_id,pl->nickname,8); pl_id[8]='\0'; }

      if (pl_id[0]) {
        pthread_mutex_lock(&ntl_lock);
        int cidx = -1;
        for (int c2=0; c2<msg_cache_count; c2++) {
          if (strcmp(msg_caches[c2].user_id, pl_id)==0) { cidx=c2; break; }
        }
        if (decoded[0]) {
          bool is_new = (cidx==-1) || strcmp(msg_caches[cidx].last_msg, decoded)!=0;
          if (is_new) {
            if (cidx==-1 && msg_cache_count < MAX_NTL_PLAYERS) cidx = msg_cache_count++;
            if (cidx >= 0) {
              strcpy(msg_caches[cidx].user_id,  pl_id);
              strcpy(msg_caches[cidx].last_msg, decoded);
              if (pending_chat_count < MAX_PENDING_CHAT) {
                strcpy(pending_chat_queue[pending_chat_count].sender, clean_nick(pl->nickname));
                strcpy(pending_chat_queue[pending_chat_count].text,   decoded);
                pending_chat_count++;
              }
            }
          }
        } else if (cidx >= 0) {
          msg_caches[cidx].last_msg[0] = '\0';
        }
        pthread_mutex_unlock(&ntl_lock);
      }
      count++;
    }

    free(obj);
    p = end + 1;
  }

  pthread_mutex_lock(&ntl_lock);
  ntl_player_count = count;
  memcpy(ntl_players_list, temp, sizeof(ntl_player) * count);
  pthread_mutex_unlock(&ntl_lock);

  NTL_LOG("Parsed %d NTL players", count);
}

/* ── Background sync thread ─────────────────────────────────────────── */

static tenv* g_env = NULL;

static void* ntl_thread_fn(void* arg) {
  tenv* env = (tenv*)arg;
  NTL_LOG("NTL sync thread started");

  while (ntl_running) {
    tuser_data*    usr  = env->usr;
    user_settings* usrs = &usr->usrs;
    game_data*     gdata = &usr->gdata;

    /* Read active team credentials */
    char active_tid[33]  = {0};
    char active_auth[65] = {0};
    if (usrs->ntl_active_team_idx >= 0 && usrs->ntl_active_team_idx < usrs->ntl_team_count) {
      strcpy(active_tid,  usrs->ntl_teams[usrs->ntl_active_team_idx].team_id);
      strcpy(active_auth, usrs->ntl_teams[usrs->ntl_active_team_idx].auth_key);
    }

    if (!active_auth[0] || !active_tid[0]) {
      NTL_LOG("No NTL team configured — sleeping 5s");
      struct timespec ts = {5, 0}; nanosleep(&ts, NULL);
      continue;
    }

    /* Generate random NTL user ID if missing */
    if (!usrs->ntl_user_id[0]) {
      for (int i = 0; i < 8; i++)
        sprintf(usrs->ntl_user_id + i, "%x", rand() % 16);
      usrs->ntl_user_id[8] = '\0';
      save_user_settings(usrs);
      NTL_LOG("Generated NTL user ID: %s", usrs->ntl_user_id);
    }

    /* Build nickname with embedded user ID */
    char nick_with_id[128];
    snprintf(nick_with_id, sizeof(nick_with_id), "%s%s", usrs->ntl_user_id, usrs->nickname);

    char enc_nick[256]={0}, enc_srv[128]={0}, enc_dt[256]={0};
    url_encode(nick_with_id, enc_nick, sizeof(enc_nick));

    if (gdata->conn == CONNECTED && gdata->curr_screen == PLAYING)
      url_encode(usrs->ipv4, enc_srv, sizeof(enc_srv));
    else
      strcpy(enc_srv, "_GAME_MENU_");

    /* Stats string */
    char raw_dt[256];
    snprintf(raw_dt, sizeof(raw_dt), "FPS:%d@%dms@%dK",
             gdata->data.fps, gdata->data.ping, gdata->data.kills);
    url_encode(raw_dt, enc_dt, sizeof(enc_dt));

    /* Snake position — use view coordinates (safe float reads, no tdarray) */
    float valx = gdata->data.view_xx;
    float valy = gdata->data.view_yy;

    /* Outgoing chat message */
    char raw_msg[512]={0};
    pthread_mutex_lock(&ntl_lock);
    for (int i=0; i<outgoing_chat_count; i++) {
      if (i>0 && strlen(raw_msg)+3 < sizeof(raw_msg)) strcat(raw_msg," | ");
      if (strlen(raw_msg)+strlen(outgoing_chat_queue[i]) < sizeof(raw_msg))
        strcat(raw_msg, outgoing_chat_queue[i]);
    }
    outgoing_chat_count = 0;
    pthread_mutex_unlock(&ntl_lock);

    char enc_msg[1024]={0};
    if (raw_msg[0]) url_encode(raw_msg, enc_msg, sizeof(enc_msg));

    int my_sid = (gdata->conn==CONNECTED && gdata->curr_screen==PLAYING)
                 ? gdata->data.snake_id : -1;

    /* Allocate HTTP context */
    ntl_http_ctx* ctx = calloc(1, sizeof(ntl_http_ctx));
    if (!ctx) { struct timespec ts={3,0}; nanosleep(&ts,NULL); continue; }

    snprintf(ctx->path, sizeof(ctx->path),
      "/slither/ntlplay-mt.php?auth=%s&tid=%s&nick=%s&score=%d"
      "&valx=%d&valy=%d&bot=%s&sos=false&food=false&srv=%s"
      "&msg=%s&dt=%s&i=%d&ver=9.19&di=1000",
      active_auth, active_tid, enc_nick, gdata->data.score,
      (int)valx, (int)valy,
      usrs->hotkeys[HOTKEY_BOT].active ? "true" : "false",
      enc_srv, enc_msg, enc_dt, my_sid);

    NTL_LOG("NTL sync → nick='%s' tid='%s' srv='%s'", nick_with_id, active_tid, enc_srv);

    /* Perform HTTPS request via Mongoose in this thread's own mgr */
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    struct mg_connection* c = mg_http_connect(&mgr,
        "https://ntl-slither.com", ntl_http_cb, ctx);

    if (c) {
      /* Poll until response received or 6-second timeout */
      int elapsed_ms = 0;
      while (!ctx->done && ntl_running && elapsed_ms < 6000) {
        mg_mgr_poll(&mgr, 100);
        elapsed_ms += 100;
      }
    } else {
      NTL_LOG("mg_http_connect failed");
    }

    mg_mgr_free(&mgr);

    if (ctx->body_len > 0)
      parse_and_store_response(ctx->body, nick_with_id);
    else
      NTL_LOG("NTL: empty/no response");

    free(ctx);

    /* Sleep 3 seconds between polls */
    for (int i = 0; i < 30 && ntl_running; i++) {
      struct timespec ts = {0, 100000000L}; /* 100ms */
      nanosleep(&ts, NULL);
    }
  }

  NTL_LOG("NTL sync thread stopped");
  return NULL;
}

/* ── Public API ─────────────────────────────────────────────────────── */

void ntl_client_start(tenv* env) {
  if (ntl_started) return;
  g_env        = env;
  ntl_running  = true;
  ntl_started  = true;
  pthread_create(&ntl_thread_id, NULL, ntl_thread_fn, env);
}

void ntl_client_stop(void) {
  if (!ntl_started) return;
  ntl_running = false;
  pthread_join(ntl_thread_id, NULL);
  ntl_started = false;
  ntl_player_count = 0;
}

ntl_player* ntl_get_players(int* count) {
  pthread_mutex_lock(&ntl_lock);
  *count = ntl_player_count;
  pthread_mutex_unlock(&ntl_lock);
  return ntl_players_list;
}

void ntl_client_send_msg(const char* text) {
  pthread_mutex_lock(&ntl_lock);
  if (outgoing_chat_count < MAX_OUTGOING_CHAT) {
    strncpy(outgoing_chat_queue[outgoing_chat_count], text, 127);
    outgoing_chat_queue[outgoing_chat_count][127] = '\0';
    outgoing_chat_count++;
  }
  pthread_mutex_unlock(&ntl_lock);
}

bool ntl_client_poll_chat(char* sender, char* text) {
  bool has = false;
  pthread_mutex_lock(&ntl_lock);
  if (pending_chat_count > 0) {
    strcpy(sender, pending_chat_queue[0].sender);
    strcpy(text,   pending_chat_queue[0].text);
    for (int i = 1; i < pending_chat_count; i++)
      pending_chat_queue[i-1] = pending_chat_queue[i];
    pending_chat_count--;
    has = true;
  }
  pthread_mutex_unlock(&ntl_lock);
  return has;
}
