#include "leaderboard.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../external/mongoose.h"
#include "../imgui_setup.h"
#include "../network/server.h"
#include "../user.h"
#ifdef ANDROID
#include "../android_path.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R, G, B, A)                                          \
  (((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | ((ImU32)(R) << 0))
#endif

#include "../android_glfw_shim.h"

void decode_secret(const uint8_t* packet, size_t packet_len, uint8_t* result);

#define LB_MAX_SERVERS 100
#define LB_TOP 10
#define LB_MM_MAX 128
#define LB_MM_BYTES ((LB_MM_MAX * LB_MM_MAX + 7) / 8)
#define LB_COOLDOWN_S 300
#define LB_GLOBAL_RUNS 5
#define LB_TIMEOUT_MS 25000
#define LB_MAP_WAIT_MS 12000
#define LB_BOT_NICK "Vlither android.bot"
#define LB_FILE "leaderboard.dat"
#define LB_MAGIC_V1 0x3153424c
#define LB_MAGIC_V2 0x3253424c

typedef struct lb_saved_v1 {
  char address[MAX_SERVER_IP_LEN + 1];
  int32_t sid;
  int64_t last_run;
  int64_t data_time;
  int32_t total_players;
  int32_t top_count;
  int32_t score[LB_TOP];
  char name[LB_TOP][MAX_NICKNAME_LEN + 1];
  int32_t mm_size;
  uint8_t mm[LB_MM_BYTES];
  char error[48];
} lb_saved_v1;

typedef struct lb_saved {
  char address[MAX_SERVER_IP_LEN + 1];
  int32_t sid;
  int64_t last_run;
  int64_t data_time;
  int32_t total_players;
  int32_t top_count;
  int32_t score[LB_TOP];
  char name[LB_TOP][MAX_NICKNAME_LEN + 1];
  int32_t mm_size;
  uint8_t mm[LB_MM_BYTES];
  char error[48];
  char cc[4];
  int32_t ping_ms;
} lb_saved;

enum { LB_ST_CONNECT, LB_ST_WS, LB_ST_LOGIN, LB_ST_JOINED, LB_ST_DATA };

typedef struct lb_rt {
  struct mg_connection* c;
  bool running;
  bool got_lb, got_mm;
  bool logged, wfpr;
  int stage;
  uint64_t started_ms, last_ping_ms, lb_ms;
  uint8_t* pix;
  int pix_size;
  int mscps;
  lb_saved stage_data;
} lb_rt;

static lb_saved s_srv[LB_MAX_SERVERS];
static lb_rt s_rt[LB_MAX_SERVERS];
static int s_n = 0;
static int64_t s_global_runs[LB_GLOBAL_RUNS];
static bool s_loaded = false;

static struct mg_mgr s_mgr;
static bool s_mgr_init = false;
static int s_geo_pending = 0;
static tenv* s_env = NULL;

static char s_input[MAX_SERVER_IP_LEN + 1];
static char s_message[96];
static bool s_message_bad = false;

static int64_t now_s(void) { return (int64_t)time(NULL); }

static FILE* lb_open(const char* mode) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, (int)sizeof path, LB_FILE);
  return fopen(path, mode);
#else
  return fopen(LB_FILE, mode);
#endif
}

static void lb_save(void) {
  FILE* f = lb_open("wb");
  if (!f) return;
  int32_t hdr[3] = {LB_MAGIC_V2, (int32_t)sizeof(lb_saved), s_n};
  fwrite(hdr, sizeof hdr, 1, f);
  fwrite(s_global_runs, sizeof s_global_runs, 1, f);
  for (int i = 0; i < s_n; i++) fwrite(&s_srv[i], sizeof(lb_saved), 1, f);
  fclose(f);
}

static void lb_clean(lb_saved* s) {
  s->address[MAX_SERVER_IP_LEN] = 0;
  s->error[sizeof s->error - 1] = 0;
  s->cc[sizeof s->cc - 1] = 0;
  if (s->top_count < 0 || s->top_count > LB_TOP) s->top_count = 0;
  if (s->mm_size < 0 || s->mm_size > LB_MM_MAX) s->mm_size = 0;
  for (int k = 0; k < LB_TOP; k++) s->name[k][MAX_NICKNAME_LEN] = 0;
}

static void lb_load(void) {
  if (s_loaded) return;
  s_loaded = true;
  FILE* f = lb_open("rb");
  if (!f) return;
  int32_t hdr[3];
  if (fread(hdr, sizeof hdr, 1, f) == 1 && hdr[2] >= 0 &&
      hdr[2] <= LB_MAX_SERVERS &&
      fread(s_global_runs, sizeof s_global_runs, 1, f) == 1) {
    int n = 0;
    if (hdr[0] == LB_MAGIC_V2 && hdr[1] == (int32_t)sizeof(lb_saved)) {
      for (int i = 0; i < hdr[2]; i++) {
        if (fread(&s_srv[n], sizeof(lb_saved), 1, f) != 1) break;
        lb_clean(&s_srv[n]);
        n++;
      }
    } else if (hdr[0] == LB_MAGIC_V1 && hdr[1] == (int32_t)sizeof(lb_saved_v1)) {
      for (int i = 0; i < hdr[2]; i++) {
        lb_saved_v1 old;
        if (fread(&old, sizeof old, 1, f) != 1) break;
        memset(&s_srv[n], 0, sizeof(lb_saved));
        memcpy(s_srv[n].address, old.address, sizeof old.address);
        s_srv[n].sid = old.sid;
        s_srv[n].last_run = old.last_run;
        s_srv[n].data_time = old.data_time;
        s_srv[n].total_players = old.total_players;
        s_srv[n].top_count = old.top_count;
        memcpy(s_srv[n].score, old.score, sizeof old.score);
        memcpy(s_srv[n].name, old.name, sizeof old.name);
        s_srv[n].mm_size = old.mm_size;
        memcpy(s_srv[n].mm, old.mm, sizeof old.mm);
        memcpy(s_srv[n].error, old.error, sizeof old.error);
        lb_clean(&s_srv[n]);
        n++;
      }
    }
    s_n = n;
  }
  fclose(f);
}

static bool lb_is_custom(int i) {
  return s_env && server_is_custom_server(s_env, s_srv[i].address);
}

static int server_wait(int i) {
  if (lb_is_custom(i)) return 0;
  if (s_srv[i].last_run <= 0) return 0;
  int64_t w = s_srv[i].last_run + LB_COOLDOWN_S - now_s();
  return w > 0 ? (int)w : 0;
}

static int global_wait(int* used) {
  int64_t now = now_s();
  int active = 0;
  int64_t oldest = 0;
  for (int k = 0; k < LB_GLOBAL_RUNS; k++) {
    int64_t t = s_global_runs[k];
    if (t > 0 && t > now - LB_COOLDOWN_S && t <= now + 5) {
      active++;
      if (oldest == 0 || t < oldest) oldest = t;
    }
  }
  if (used) *used = active;
  if (active < LB_GLOBAL_RUNS) return 0;
  int64_t w = oldest + LB_COOLDOWN_S - now;
  return w > 0 ? (int)w : 0;
}

static void global_note_run(void) {
  int64_t now = now_s();
  int slot = 0;
  for (int k = 0; k < LB_GLOBAL_RUNS; k++) {
    int64_t t = s_global_runs[k];
    if (t <= 0 || t <= now - LB_COOLDOWN_S) {
      slot = k;
      break;
    }
    if (t < s_global_runs[slot]) slot = k;
  }
  s_global_runs[slot] = now;
}

static int lb_score(int mscps, int sct, float fam) {
  if (mscps <= 0) return 0;
  int n = sct < mscps ? sct : mscps;
  float fpsl = 0;
  for (int k = 0; k < n; k++)
    fpsl += 1.0f / powf(1.0f - (float)k / (float)mscps, 2.25f);
  int fi = sct < mscps ? sct : mscps - 1;
  float fmlt = powf(1.0f - (float)fi / (float)mscps, 2.25f);
  return (int)floorf((fpsl + fam / fmlt - 1.0f) * 15.0f - 5.0f);
}

static const char* lb_stage_fail(int stage) {
  switch (stage) {
    case LB_ST_CONNECT: return "Could not connect to the server";
    case LB_ST_WS: return "Server sent no handshake";
    case LB_ST_LOGIN: return "Server did not let the bot join";
    case LB_ST_JOINED: return "Joined, but no leaderboard arrived";
    default: return "Failed";
  }
}

static const char* lb_stage_text(int stage) {
  switch (stage) {
    case LB_ST_CONNECT: return "Connecting";
    case LB_ST_WS: return "Handshake";
    case LB_ST_LOGIN: return "Joining";
    case LB_ST_JOINED: return "Waiting";
    default: return "Reading map";
  }
}

static void lb_commit(int i, bool save) {
  lb_rt* rt = &s_rt[i];
  lb_saved* s = &s_srv[i];
  lb_saved* g = &rt->stage_data;
  if (rt->got_lb) {
    s->data_time = now_s();
    s->total_players = g->total_players;
    s->top_count = g->top_count;
    memcpy(s->score, g->score, sizeof s->score);
    memcpy(s->name, g->name, sizeof s->name);
    s->error[0] = 0;
  }
  if (rt->got_mm) {
    s->mm_size = g->mm_size;
    memcpy(s->mm, g->mm, sizeof s->mm);
  }
  if (g->ping_ms > 0) s->ping_ms = g->ping_ms;
  if (save) lb_save();
}

static void lb_finish(int i, bool ok, const char* err) {
  lb_rt* rt = &s_rt[i];
  if (rt->c) {
    rt->c->fn_data = NULL;
    rt->c->is_closing = 1;
    rt->c = NULL;
  }
  bool was_running = rt->running;
  rt->running = false;
  free(rt->pix);
  rt->pix = NULL;
  rt->pix_size = 0;
  if (!was_running) return;
  if (ok) {
    lb_commit(i, true);
  } else {
    snprintf(s_srv[i].error, sizeof s_srv[i].error, "%s",
             err ? err : lb_stage_fail(rt->stage));
    lb_save();
  }
}

static void lb_send(struct mg_connection* c, const void* b, size_t n) {
  mg_ws_send(c, b, n, WEBSOCKET_OP_BINARY);
}

static void lb_send_login(int i, struct mg_connection* c) {
  user_settings* usrs = &s_env->usr->usrs;
  if (server_bd_id_should_send(s_srv[i].address)) {
    ensure_bd_client_id(usrs);
    uint8_t bd[1 + 16];
    bd[0] = BD_ID_OPCODE;
    memcpy(bd + 1, usrs->bd_client_id, 16);
    lb_send(c, bd, sizeof bd);
  }
  static const uint8_t cwa[20] = {54, 206, 204, 169, 97,  178, 74,  136, 124, 117,
                                  14, 210, 106, 236, 8,   208, 136, 213, 140, 111};
  const char* nick = LB_BOT_NICK;
  int nl = (int)strlen(nick);
  uint8_t ba[26 + sizeof LB_BOT_NICK + 2];
  int m = 0;
  ba[m++] = 115;
  ba[m++] = 30;
  ba[m++] = CLIENT_VERSION >> 8 & 255;
  ba[m++] = CLIENT_VERSION & 255;
  memcpy(ba + m, cwa, 20);
  m += 20;
  ba[m++] = 0;
  ba[m++] = (uint8_t)nl;
  memcpy(ba + m, nick, (size_t)nl);
  m += nl;
  ba[m++] = 0;
  ba[m++] = NO_ACCESSORY;
  lb_send(c, ba, (size_t)m);
  s_rt[i].logged = true;
  s_rt[i].stage = LB_ST_LOGIN;
  s_rt[i].last_ping_ms = mg_millis();
}

static void lb_parse_leaderboard(int i, const uint8_t* a, int len) {
  lb_rt* rt = &s_rt[i];
  lb_saved* g = &rt->stage_data;
  if (len < 6) return;
  int m = 1;
  int lb_pos = a[m++];
  m += 2;
  int count = a[m] << 8 | a[m + 1];
  m += 2;
  g->total_players = count > 0 ? count - 1 : 0;
  g->top_count = 0;
  int pos = 0;
  while (m + 7 <= len) {
    pos++;
    int sct = a[m] << 8 | a[m + 1];
    m += 2;
    float fam = (a[m] << 16 | a[m + 1] << 8 | a[m + 2]) / 16777215.0f;
    m += 3;
    m++;
    int nl = a[m++];
    if (m + nl > len) break;
    if (pos != lb_pos && g->top_count < LB_TOP) {
      int k = g->top_count++;
      g->score[k] = lb_score(rt->mscps, sct, fam);
      int cp = nl < MAX_NICKNAME_LEN ? nl : MAX_NICKNAME_LEN;
      for (int j = 0; j < cp; j++) {
        uint8_t ch = a[m + j];
        g->name[k][j] = ch < 32 ? ' ' : (char)ch;
      }
      g->name[k][cp] = 0;
    }
    m += nl;
  }
  bool first = !rt->got_lb;
  rt->got_lb = true;
  rt->stage = LB_ST_DATA;
  if (first) rt->lb_ms = mg_millis();
  lb_commit(i, first);
}

static void lb_mm_decode(uint8_t* pix, int total, const uint8_t* a, int len,
                         int off, bool reverse, bool toggle, bool ext) {
  long cur = reverse ? total - 1 : 0;
  int step = reverse ? -1 : 1;
  while (off < len && cur >= 0 && cur < total) {
    int v = a[off++];
    if (v >= 128) {
      int skip;
      if (ext && v == 255 && off < len) skip = 126 * a[off++];
      else skip = v - 128;
      cur += step * skip;
      continue;
    }
    for (int mask = 64; mask >= 1 && cur >= 0 && cur < total; mask >>= 1) {
      if (v & mask) {
        if (toggle) pix[cur] ^= 1;
        else pix[cur] = 1;
      }
      cur += step;
    }
  }
}

static void lb_mm_pack(lb_rt* rt) {
  lb_saved* g = &rt->stage_data;
  int sz = rt->pix_size;
  int out = sz < LB_MM_MAX ? sz : LB_MM_MAX;
  memset(g->mm, 0, sizeof g->mm);
  for (int y = 0; y < sz; y++)
    for (int x = 0; x < sz; x++) {
      if (!rt->pix[y * sz + x]) continue;
      int dx = out == sz ? x : x * out / sz;
      int dy = out == sz ? y : y * out / sz;
      int bit = dy * out + dx;
      g->mm[bit >> 3] |= (uint8_t)(1 << (bit & 7));
    }
  g->mm_size = out;
}

static void lb_parse_minimap(int i, const uint8_t* a, int len) {
  lb_rt* rt = &s_rt[i];
  char cmd = (char)a[0];
  if (cmd == 'V') {
    if (!rt->pix) return;
    lb_mm_decode(rt->pix, rt->pix_size * rt->pix_size, a, len, 1, true, true, true);
    lb_mm_pack(rt);
    if (rt->got_mm) lb_commit(i, false);
    return;
  }
  int size = 80;
  int off = 1;
  if (cmd == 'U' || cmd == 'M') {
    if (len < 3) return;
    size = a[1] << 8 | a[2];
    if (size <= 0) size = 80;
    if (size > 512) size = 512;
    off = 3;
  } else if (cmd != 'u') {
    return;
  }
  free(rt->pix);
  rt->pix = (uint8_t*)calloc((size_t)size * (size_t)size, 1);
  if (!rt->pix) {
    rt->pix_size = 0;
    return;
  }
  rt->pix_size = size;
  lb_mm_decode(rt->pix, size * size, a, len, off, cmd != 'u', false, cmd == 'M');
  lb_mm_pack(rt);
  rt->got_mm = true;
  lb_commit(i, true);
}

static void lb_packet(int i, struct mg_connection* c, const uint8_t* a, int len) {
  lb_rt* rt = &s_rt[i];
  switch ((char)a[0]) {
    case '6': {
      uint8_t secret[27] = {0};
      decode_secret(a, (size_t)len, secret);
      lb_send(c, secret, 27);
      lb_send_login(i, c);
      break;
    }
    case 'a':
      if (len >= 6) rt->mscps = a[4] << 8 | a[5];
      if (rt->stage < LB_ST_JOINED) rt->stage = LB_ST_JOINED;
      break;
    case 'p':
      rt->wfpr = false;
      break;
    case 'l':
      lb_parse_leaderboard(i, a, len);
      break;
    case 'M':
    case 'U':
    case 'u':
    case 'V':
      lb_parse_minimap(i, a, len);
      break;
    default:
      break;
  }
  if (rt->got_lb && rt->got_mm) lb_finish(i, true, NULL);
}

static void lb_cb(struct mg_connection* c, int ev, void* ev_data) {
  int i = (int)(intptr_t)c->fn_data - 1;
  if (ev == MG_EV_OPEN) return;
  if (i < 0 || i >= s_n || s_rt[i].c != c) {
    if (ev == MG_EV_WS_OPEN || ev == MG_EV_WS_MSG) c->is_closing = 1;
    return;
  }
  if (ev == MG_EV_WS_OPEN) {
    s_rt[i].stage_data.ping_ms = (int)(mg_millis() - s_rt[i].started_ms);
    if (s_rt[i].stage < LB_ST_WS) s_rt[i].stage = LB_ST_WS;
    lb_send(c, (uint8_t[]){1}, 1);
    lb_send(c, (uint8_t[]){'c', 0}, 2);
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message* msg = (struct mg_ws_message*)ev_data;
    const uint8_t* a = (const uint8_t*)msg->data.buf;
    int l = (int)msg->data.len;
    if (l <= 0) return;
    if (a[0] < 32) {
      int m = 0;
      while (m < l && s_rt[i].c == c) {
        int len;
        if (a[m] < 32) {
          if (l - m < 2) break;
          len = a[m] << 8 | a[m + 1];
          m += 2;
        } else {
          len = a[m] - 32;
          m++;
        }
        if (len <= 0 || len > l - m) break;
        lb_packet(i, c, a + m, len);
        m += len;
      }
    } else {
      lb_packet(i, c, a, l);
    }
  } else if (ev == MG_EV_ERROR) {
    lb_finish(i, s_rt[i].got_lb, s_rt[i].stage == LB_ST_CONNECT ? "Could not connect to the server" : NULL);
  } else if (ev == MG_EV_CLOSE) {
    s_rt[i].c = NULL;
    if (s_rt[i].running) lb_finish(i, s_rt[i].got_lb, NULL);
  }
}

static void lb_connect(int i) {
  if (!s_mgr_init) {
    mg_mgr_init(&s_mgr);
    s_mgr_init = true;
  }
  const char* ad = s_srv[i].address;
  bool is_local = strncmp(ad, "127.", 4) == 0 || strncmp(ad, "[::1]", 5) == 0 ||
                  strncmp(ad, "localhost", 9) == 0 ||
                  strncmp(ad, "192.168.", 8) == 0 || strncmp(ad, "10.", 3) == 0;
  char url[160];
  snprintf(url, sizeof url, "%s://%s/slither", is_local ? "ws" : "wss", ad);
  lb_rt* rt = &s_rt[i];
  memset(rt, 0, sizeof *rt);
  rt->running = true;
  rt->started_ms = mg_millis();
  rt->c = mg_ws_connect(&s_mgr, url, lb_cb, (void*)(intptr_t)(i + 1),
                        "%s:%s\r\n%s:%s\r\n", "Origin", "https://slither.com",
                        "Host", "slither.com");
  if (!rt->c) lb_finish(i, false, "Could not connect to the server");
}

void leaderboard_update(tenv* env) {
  if (env) s_env = env;
  if (!s_mgr_init) return;
  mg_mgr_poll(&s_mgr, 0);
  uint64_t now = mg_millis();
  for (int i = 0; i < s_n; i++) {
    if (!s_rt[i].running) continue;
    lb_rt* rt = &s_rt[i];
    if (rt->c && rt->logged) {
      if (rt->wfpr && now - rt->last_ping_ms > 2000) rt->wfpr = false;
      if (!rt->wfpr && now - rt->last_ping_ms >= 250) {
        lb_send(rt->c, (uint8_t[]){251}, 1);
        lb_send(rt->c, (uint8_t[]){0}, 1);
        rt->wfpr = true;
        rt->last_ping_ms = now;
      }
    }
    if (rt->got_lb) {
      if (rt->got_mm || now - rt->lb_ms > LB_MAP_WAIT_MS) lb_finish(i, true, NULL);
    } else if (now - rt->started_ms > LB_TIMEOUT_MS) {
      lb_finish(i, false, NULL);
    }
  }
}

typedef struct lb_geo {
  char address[MAX_SERVER_IP_LEN + 1];
} lb_geo;

static void host_of(const char* address, char* out, size_t n) {
  snprintf(out, n, "%s", address);
  if (out[0] == '[') {
    char* e = strchr(out, ']');
    if (e) {
      *e = 0;
      memmove(out, out + 1, strlen(out + 1) + 1);
    }
    return;
  }
  char* colon = strrchr(out, ':');
  if (colon) *colon = 0;
}

static void lb_geo_cb(struct mg_connection* c, int ev, void* ev_data) {
  lb_geo* g = (lb_geo*)c->fn_data;
  if (ev == MG_EV_CONNECT) {
    if (!g) return;
    char host[MAX_SERVER_IP_LEN + 1];
    host_of(g->address, host, sizeof host);
    mg_printf(c,
              "GET /json/%s?fields=status,countryCode HTTP/1.1\r\n"
              "Host: ip-api.com\r\nUser-Agent: Vlither\r\n"
              "Connection: close\r\n\r\n",
              host);
  } else if (ev == MG_EV_HTTP_MSG && g) {
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;
    char* code = mg_json_get_str(hm->body, "$.countryCode");
    if (code) {
      if (strlen(code) == 2) {
        for (int i = 0; i < s_n; i++)
          if (strcmp(s_srv[i].address, g->address) == 0) {
            s_srv[i].cc[0] = (char)((code[0] >= 'a' && code[0] <= 'z') ? code[0] - 32 : code[0]);
            s_srv[i].cc[1] = (char)((code[1] >= 'a' && code[1] <= 'z') ? code[1] - 32 : code[1]);
            s_srv[i].cc[2] = 0;
          }
        lb_save();
      }
      mg_free(code);
    }
    c->is_draining = 1;
  } else if (ev == MG_EV_CLOSE) {
    if (g) {
      free(g);
      c->fn_data = NULL;
      if (s_geo_pending > 0) s_geo_pending--;
    }
  }
}

static void lb_geo_start(int i) {
  if (!s_mgr_init) {
    mg_mgr_init(&s_mgr);
    s_mgr_init = true;
  }
  lb_geo* g = (lb_geo*)calloc(1, sizeof *g);
  if (!g) return;
  snprintf(g->address, sizeof g->address, "%s", s_srv[i].address);
  struct mg_connection* c =
      mg_http_connect(&s_mgr, "http://ip-api.com", lb_geo_cb, g);
  if (!c) {
    free(g);
    return;
  }
  s_geo_pending++;
}

static int lb_find_sid(const char* address) {
  if (!s_env) return 0;
  game_data* gd = &s_env->usr->gdata;
  for (int k = gd->server_list.custom_count; k < gd->server_list.count; ++k)
    if (gd->server_list.families[k] == 4 &&
        strcmp(gd->server_list.ips[k], address) == 0)
      return (int)gd->server_list.sids[k];
  return 0;
}

static void lb_cancel_running(void) {
  for (int i = 0; i < s_n; i++) {
    if (!s_rt[i].running) continue;
    if (s_rt[i].c) {
      s_rt[i].c->fn_data = NULL;
      s_rt[i].c->is_closing = 1;
      s_rt[i].c = NULL;
    }
    s_rt[i].running = false;
  }
}

static void lb_play(int i) {
  if (!s_env || i < 0 || i >= s_n) return;
  tuser_data* usr = s_env->usr;
  lb_cancel_running();
  snprintf(usr->usrs.server_address, sizeof usr->usrs.server_address, "%s",
           s_srv[i].address);
  save_user_settings(&usr->usrs);
  usr->gdata.conn = CONNECTING;
  usr->gdata.curr_screen = PLAYING;
  glfwSetTime(0);
  server_connect(s_env);
}

static bool lb_start(int i) {
  if (s_env == NULL || i < 0 || i >= s_n || s_rt[i].running) return false;
  bool custom = lb_is_custom(i);
  if (!custom && (server_wait(i) > 0 || global_wait(NULL) > 0)) return false;
  s_srv[i].last_run = now_s();
  if (!custom) global_note_run();
  lb_save();
  if (!s_srv[i].cc[0]) lb_geo_start(i);
  lb_connect(i);
  return true;
}

static void set_message(bool bad, const char* fmt, const char* arg) {
  s_message_bad = bad;
  snprintf(s_message, sizeof s_message, fmt, arg ? arg : "");
}

static void lb_add_from_input(tenv* env) {
  char in[MAX_SERVER_IP_LEN + 1];
  size_t n = 0;
  for (const char* p = s_input; *p && n < sizeof in - 1; p++)
    if (*p != ' ' && *p != '\t') in[n++] = *p;
  in[n] = 0;
  if (n == 0) {
    set_message(true, "Enter a server IP:port or a SID.%s", NULL);
    return;
  }
  char address[MAX_SERVER_IP_LEN + 8] = {0};
  int sid = 0;
  bool digits = true;
  for (size_t k = 0; k < n; k++)
    if (in[k] < '0' || in[k] > '9') digits = false;
  if (digits && n <= 5) {
    sid = atoi(in);
    if (!server_list_resolve_sid(env, sid, address, sizeof address)) {
      set_message(true, "SID not found. Open the server list on the homepage first.%s", NULL);
      return;
    }
  } else {
    for (size_t k = 0; k < n; k++) {
      char ch = in[k];
      bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == ':' ||
                ch == '-' || ch == '[' || ch == ']';
      if (!ok) {
        set_message(true, "That does not look like an IP address.%s", NULL);
        return;
      }
    }
    if (strchr(in, ':') == NULL || (in[0] == '[' && strstr(in, "]:") == NULL))
      snprintf(address, sizeof address, "%s:444", in);
    else
      snprintf(address, sizeof address, "%s", in);
  }
  if (strlen(address) > MAX_SERVER_IP_LEN) {
    set_message(true, "Address is too long.%s", NULL);
    return;
  }
  for (int i = 0; i < s_n; i++)
    if (strcmp(s_srv[i].address, address) == 0) {
      set_message(true, "%s is already saved.", address);
      return;
    }
  if (s_n >= LB_MAX_SERVERS) {
    set_message(true, "Too many saved servers.%s", NULL);
    return;
  }
  memset(&s_srv[s_n], 0, sizeof(lb_saved));
  memset(&s_rt[s_n], 0, sizeof(lb_rt));
  snprintf(s_srv[s_n].address, sizeof s_srv[s_n].address, "%s", address);
  s_srv[s_n].sid = sid ? sid : lb_find_sid(address);
  s_n++;
  s_input[0] = 0;
  lb_save();
  set_message(false, "Saved %s. Tap Start to load it.", address);
}

static void lb_remove(int i) {
  if (i < 0 || i >= s_n) return;
  if (s_rt[i].running) lb_finish(i, false, "removed");
  for (int k = i; k < s_n - 1; k++) {
    s_srv[k] = s_srv[k + 1];
    s_rt[k] = s_rt[k + 1];
    if (s_rt[k].c) s_rt[k].c->fn_data = (void*)(intptr_t)(k + 1);
  }
  s_n--;
  lb_save();
}

void ui_leaderboard_panel_open(void) {
  lb_load();
  s_message[0] = 0;
}

static void fmt_clock(int64_t t, char* out, size_t n) {
  time_t tt = (time_t)t;
  struct tm tmv;
#ifdef _WIN32
  gmtime_s(&tmv, &tt);
#else
  gmtime_r(&tt, &tmv);
#endif
  strftime(out, n, "%a, %d %b %Y %H:%M:%S UTC", &tmv);
}

static void fmt_age(int64_t t, char* out, size_t n) {
  int64_t d = now_s() - t;
  if (d < 60) snprintf(out, n, "just now");
  else if (d < 3600) snprintf(out, n, "%d min ago", (int)(d / 60));
  else if (d < 86400) snprintf(out, n, "%d h ago", (int)(d / 3600));
  else snprintf(out, n, "%d d ago", (int)(d / 86400));
}

static void fmt_mmss(int sec, char* out, size_t n) {
  snprintf(out, n, "%d:%02d", sec / 60, sec % 60);
}

static void fmt_num(long long v, char* out, size_t n) {
  char raw[32];
  snprintf(raw, sizeof raw, "%lld", v < 0 ? -v : v);
  int len = (int)strlen(raw), o = 0;
  char tmp[48];
  if (v < 0) tmp[o++] = '-';
  for (int k = 0; k < len; k++) {
    if (k > 0 && (len - k) % 3 == 0) tmp[o++] = ',';
    tmp[o++] = raw[k];
  }
  tmp[o] = 0;
  snprintf(out, n, "%s", tmp);
}

#define COL(r, g, b, a) IM_COL32(r, g, b, a)

typedef struct lb_flag {
  const char cc[3];
  char kind;
  ImU32 c1, c2, c3;
} lb_flag;

static const lb_flag s_flags[] = {
    {"IN", 'H', COL(255, 153, 51, 255), COL(255, 255, 255, 255), COL(19, 136, 8, 255)},
    {"SG", 'T', COL(237, 41, 57, 255), COL(255, 255, 255, 255), 0},
    {"US", 'U', 0, 0, 0},
    {"GB", 'K', 0, 0, 0},
    {"FR", 'V', COL(0, 85, 164, 255), COL(255, 255, 255, 255), COL(239, 65, 53, 255)},
    {"ES", 'E', COL(170, 21, 27, 255), COL(241, 191, 0, 255), 0},
    {"DE", 'H', COL(0, 0, 0, 255), COL(221, 0, 0, 255), COL(255, 206, 0, 255)},
    {"NL", 'H', COL(174, 28, 40, 255), COL(255, 255, 255, 255), COL(33, 70, 139, 255)},
    {"IT", 'V', COL(0, 146, 70, 255), COL(255, 255, 255, 255), COL(206, 43, 55, 255)},
    {"RU", 'H', COL(255, 255, 255, 255), COL(0, 57, 166, 255), COL(213, 43, 30, 255)},
    {"JP", 'D', COL(255, 255, 255, 255), COL(188, 0, 45, 255), 0},
    {"BR", 'D', COL(0, 155, 58, 255), COL(254, 223, 0, 255), 0},
    {"CA", 'e', COL(216, 6, 33, 255), COL(255, 255, 255, 255), 0},
    {"AU", 'D', COL(0, 0, 139, 255), COL(0, 0, 139, 255), 0},
    {"PL", 'T', COL(255, 255, 255, 255), COL(220, 20, 60, 255), 0},
    {"UA", 'T', COL(0, 87, 183, 255), COL(255, 215, 0, 255), 0},
    {"ID", 'T', COL(206, 17, 38, 255), COL(255, 255, 255, 255), 0},
    {"MX", 'V', COL(0, 104, 71, 255), COL(255, 255, 255, 255), COL(206, 17, 43, 255)},
    {"AR", 'H', COL(116, 172, 223, 255), COL(255, 255, 255, 255), COL(116, 172, 223, 255)},
    {"KR", 'D', COL(255, 255, 255, 255), COL(205, 46, 58, 255), 0},
    {"TR", 'D', COL(227, 10, 23, 255), COL(255, 255, 255, 255), 0},
};

static bool draw_flag(ImDrawList* dl, float x, float y, float w, float h,
                      const char* cc) {
  for (size_t k = 0; k < sizeof s_flags / sizeof s_flags[0]; k++) {
    const lb_flag* f = &s_flags[k];
    if (strcmp(f->cc, cc) != 0) continue;
    ImVec2 a = {x, y}, b = {x + w, y + h};
    switch (f->kind) {
      case 'H':
        ImDrawList_AddRectFilled(dl, a, (ImVec2){b.x, y + h / 3}, f->c1, 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x, y + h / 3}, (ImVec2){b.x, y + 2 * h / 3}, f->c2, 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x, y + 2 * h / 3}, b, f->c3, 0, 0);
        break;
      case 'V':
        ImDrawList_AddRectFilled(dl, a, (ImVec2){x + w / 3, b.y}, f->c1, 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x + w / 3, y}, (ImVec2){x + 2 * w / 3, b.y}, f->c2, 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x + 2 * w / 3, y}, b, f->c3, 0, 0);
        break;
      case 'T':
        ImDrawList_AddRectFilled(dl, a, (ImVec2){b.x, y + h / 2}, f->c1, 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x, y + h / 2}, b, f->c2, 0, 0);
        break;
      case 'E':
        ImDrawList_AddRectFilled(dl, a, b, f->c1, 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x, y + h / 4}, (ImVec2){b.x, y + 3 * h / 4}, f->c2, 0, 0);
        break;
      case 'e':
        ImDrawList_AddRectFilled(dl, a, b, f->c1, 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x + w / 4, y}, (ImVec2){x + 3 * w / 4, b.y}, f->c2, 0, 0);
        break;
      case 'D':
        ImDrawList_AddRectFilled(dl, a, b, f->c1, 0, 0);
        ImDrawList_AddCircleFilled(dl, (ImVec2){x + w / 2, y + h / 2}, h * 0.3f, f->c2, 16);
        break;
      case 'U':
        for (int r = 0; r < 7; r++)
          ImDrawList_AddRectFilled(dl, (ImVec2){x, y + h * r / 7.0f},
                                   (ImVec2){b.x, y + h * (r + 1) / 7.0f},
                                   (r % 2 == 0) ? COL(178, 34, 52, 255) : COL(255, 255, 255, 255), 0, 0);
        ImDrawList_AddRectFilled(dl, a, (ImVec2){x + w * 0.42f, y + h * 4 / 7.0f}, COL(60, 59, 110, 255), 0, 0);
        break;
      case 'K':
        ImDrawList_AddRectFilled(dl, a, b, COL(1, 33, 105, 255), 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x + w * 0.4f, y}, (ImVec2){x + w * 0.6f, b.y}, COL(255, 255, 255, 255), 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x, y + h * 0.38f}, (ImVec2){b.x, y + h * 0.62f}, COL(255, 255, 255, 255), 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x + w * 0.45f, y}, (ImVec2){x + w * 0.55f, b.y}, COL(200, 16, 46, 255), 0, 0);
        ImDrawList_AddRectFilled(dl, (ImVec2){x, y + h * 0.44f}, (ImVec2){b.x, y + h * 0.56f}, COL(200, 16, 46, 255), 0, 0);
        break;
    }
    ImDrawList_AddRect(dl, a, b, COL(0, 0, 0, 90), 0, 0, 1.0f);
    return true;
  }
  return false;
}

static float draw_badge(ImDrawList* dl, float right, float y, float scale,
                        const char* text, const char* flag_cc, ImU32 fg,
                        ImU32 bg, ImU32 border) {
  ImVec2 ts;
  igCalcTextSize(&ts, text, NULL, false, -1.0f);
  float padx = 7.0f * scale, pady = 2.0f * scale;
  float fw = flag_cc ? 16.0f * scale : 0, fh = 11.0f * scale;
  float gap = flag_cc ? 5.0f * scale : 0;
  float w = padx * 2 + fw + gap + ts.x, h = ts.y + pady * 2;
  float x = right - w;
  ImDrawList_AddRectFilled(dl, (ImVec2){x, y}, (ImVec2){right, y + h}, bg, h * 0.5f, 0);
  ImDrawList_AddRect(dl, (ImVec2){x, y}, (ImVec2){right, y + h}, border, h * 0.5f, 0, 1.0f);
  float tx = x + padx;
  if (flag_cc) {
    if (!draw_flag(dl, tx, y + (h - fh) * 0.5f, fw, fh, flag_cc)) {
      fw = 0;
      gap = 0;
    }
    tx += fw + gap;
  }
  ImDrawList_AddText_Vec2(dl, (ImVec2){tx, y + pady}, fg, text, NULL);
  return x;
}

static void draw_minimap(const lb_saved* s, float size) {
  ImVec2 p;
  igGetCursorScreenPos(&p);
  igDummy((ImVec2){size, size});
  ImDrawList* dl = igGetWindowDrawList();
  float r = size * 0.5f;
  float ring = size * 0.07f;
  ImVec2 c = {p.x + r, p.y + r};
  ImDrawList_AddCircleFilled(dl, c, r, COL(0, 0, 0, 255), 48);
  float ir = r - ring;
  const float pi = 3.14159265f;
  ImDrawList_AddCircleFilled(dl, c, ir, COL(74, 88, 104, 255), 48);
  ImDrawList_PathLineTo(dl, c);
  ImDrawList_PathArcTo(dl, c, ir, pi, 1.5f * pi, 16);
  ImDrawList_PathFillConvex(dl, COL(112, 128, 144, 255));
  ImDrawList_PathLineTo(dl, c);
  ImDrawList_PathArcTo(dl, c, ir, 0.0f, 0.5f * pi, 16);
  ImDrawList_PathFillConvex(dl, COL(112, 128, 144, 255));
  if (s->mm_size > 0) {
    float cell = (ir * 2.0f) / (float)s->mm_size;
    float ox = c.x - ir, oy = c.y - ir;
    ImU32 col = COL(240, 244, 250, 255);
    for (int y = 0; y < s->mm_size; y++)
      for (int x = 0; x < s->mm_size; x++) {
        int bit = y * s->mm_size + x;
        if (s->mm[bit >> 3] & (1 << (bit & 7))) {
          float px = ox + x * cell, py = oy + y * cell;
          ImDrawList_AddRectFilled(dl, (ImVec2){px, py},
                                   (ImVec2){px + cell + 0.6f, py + cell + 0.6f}, col, 0, 0);
        }
      }
  }
  ImDrawList_AddLine(dl, (ImVec2){c.x - ir, c.y}, (ImVec2){c.x + ir, c.y}, COL(45, 55, 70, 255), 1.5f);
  ImDrawList_AddLine(dl, (ImVec2){c.x, c.y - ir}, (ImVec2){c.x, c.y + ir}, COL(45, 55, 70, 255), 1.5f);
}

static void stat_box(tuser_data* usr, const char* label, const char* value,
                     float w, float h, ImU32 value_col) {
  ImVec2 p;
  igGetCursorScreenPos(&p);
  ImDrawList* dl = igGetWindowDrawList();
  ImDrawList_AddRectFilled(dl, p, (ImVec2){p.x + w, p.y + h}, COL(20, 26, 44, 255), 6.0f, 0);
  ImDrawList_AddRect(dl, p, (ImVec2){p.x + w, p.y + h}, COL(50, 60, 100, 160), 6.0f, 0, 1.0f);
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  ImVec2 ls;
  igCalcTextSize(&ls, label, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(dl, (ImVec2){p.x + 8, p.y + 6}, COL(130, 140, 170, 255), label, NULL);
  igPopFont();
  ImDrawList_AddText_Vec2(dl, (ImVec2){p.x + 8, p.y + 10 + ls.y}, value_col, value, NULL);
  igDummy((ImVec2){w, h});
}

static void fit_text(char* s, float maxw) {
  ImVec2 ts;
  for (;;) {
    igCalcTextSize(&ts, s, NULL, false, -1.0f);
    size_t n = strlen(s);
    if (ts.x <= maxw || n == 0) return;
    n--;
    while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    s[n] = 0;
  }
}

static void draw_card(int i, float scale, float card_w, tuser_data* usr) {
  lb_saved* s = &s_srv[i];
  lb_rt* rt = &s_rt[i];
  ImGuiStyle* style = igGetStyle();
  if (s->sid == 0) s->sid = lb_find_sid(s->address);

  igPushID_Int(i);
  igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){0.043f, 0.055f, 0.110f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.22f, 0.27f, 0.52f, 0.60f});
  igBeginChild_Str("##card", (ImVec2){card_w, 0},
                   ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                   ImGuiWindowFlags_NoScrollbar);
  ImDrawList* dl = igGetWindowDrawList();
  const float inner_w = card_w - style->WindowPadding.x * 2.0f;

  ImVec2 top;
  igGetCursorScreenPos(&top);
  if (s->sid > 0)
    igTextColored((ImVec4){0.35f, 0.85f, 0.98f, 1.0f}, "SID: %d", s->sid);
  else
    igTextColored((ImVec4){0.35f, 0.85f, 0.98f, 1.0f}, "Server #%d", i + 1);
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  igTextColored((ImVec4){0.62f, 0.68f, 0.82f, 1.0f}, "IPv4: %s", s->address);
  if (s->ping_ms > 0)
    igTextColored((ImVec4){0.62f, 0.68f, 0.82f, 1.0f}, "Ping: %d ms", s->ping_ms);
  else
    igTextColored((ImVec4){0.62f, 0.68f, 0.82f, 1.0f}, "Ping: -");
  {
    const char* st;
    ImU32 fg, bg, bd;
    if (rt->running) {
      st = "RUNNING";
      fg = COL(255, 205, 110, 255); bg = COL(70, 50, 10, 255); bd = COL(150, 110, 30, 255);
    } else if (s->error[0]) {
      st = "FAILED";
      fg = COL(255, 130, 120, 255); bg = COL(60, 20, 20, 255); bd = COL(150, 60, 60, 255);
    } else if (s->data_time) {
      st = "ONLINE";
      fg = COL(110, 240, 160, 255); bg = COL(10, 60, 40, 255); bd = COL(30, 130, 90, 255);
    } else {
      st = "NEW";
      fg = COL(170, 180, 205, 255); bg = COL(28, 34, 54, 255); bd = COL(70, 80, 120, 255);
    }
    float right = top.x + inner_w;
    float left = draw_badge(dl, right, top.y, scale, st, NULL, fg, bg, bd);
    if (s->cc[0])
      left = draw_badge(dl, left - 6.0f * scale, top.y, scale, s->cc, s->cc,
                        COL(210, 220, 245, 255), COL(24, 30, 52, 255), COL(70, 80, 130, 255));
    if (lb_is_custom(i))
      draw_badge(dl, left - 6.0f * scale, top.y, scale, "CUSTOM", NULL,
                 COL(205, 175, 255, 255), COL(40, 24, 70, 255), COL(110, 80, 170, 255));
  }
  igPopFont();
  igSpacing();

  float gap = 8.0f * scale;
  float map = inner_w * 0.46f;
  if (map > 170.0f * scale) map = 170.0f * scale;
  igBeginGroup();
  draw_minimap(s, map);
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  if (s->mm_size > 0) {
    char age[32];
    fmt_age(s->data_time, age, sizeof age);
    igTextDisabled("Map updated %s", age);
  } else if (s->data_time) {
    igTextDisabled("Map not received");
  } else {
    igTextDisabled("No map yet");
  }
  igPopFont();
  igEndGroup();

  igSameLine(0, gap);
  float bw = inner_w - map - gap;
  float half = (bw - gap) * 0.5f;
  float bh = map * 0.42f;
  char tot[32], players[32], age2[32];
  long long total = 0;
  for (int k = 0; k < s->top_count; k++) total += s->score[k];
  fmt_num(total, tot, sizeof tot);
  fmt_num(s->total_players, players, sizeof players);
  if (s->data_time) fmt_age(s->data_time, age2, sizeof age2);
  else snprintf(age2, sizeof age2, "-");
  igBeginGroup();
  stat_box(usr, "Players", s->data_time ? players : "-", half, bh, COL(255, 255, 255, 255));
  igSameLine(0, gap);
  stat_box(usr, "Top 10 score", s->data_time ? tot : "-", half, bh, COL(255, 255, 255, 255));
  igDummy((ImVec2){1, 4.0f * scale});
  stat_box(usr, "Leaderboard", age2, half, bh, COL(255, 255, 255, 255));
  igEndGroup();

  igDummy((ImVec2){1, 4.0f * scale});

  bool custom = lb_is_custom(i);
  int sw = server_wait(i);
  int gw = custom ? 0 : global_wait(NULL);
  bool can = !rt->running && sw == 0 && gw == 0;
  char label[48];
  if (rt->running) snprintf(label, sizeof label, "%s", lb_stage_text(rt->stage));
  else if (sw > 0) {
    char t[16];
    fmt_mmss(sw, t, sizeof t);
    snprintf(label, sizeof label, "Wait %s", t);
  } else if (gw > 0) {
    char t[16];
    fmt_mmss(gw, t, sizeof t);
    snprintf(label, sizeof label, "Limit %s", t);
  } else {
    snprintf(label, sizeof label, "%s", s->data_time ? "Reload" : "Start");
  }
  strncat(label, "##go", sizeof label - strlen(label) - 1);
  float xw = 34.0f * scale;
  float bwid = (inner_w - xw - style->ItemSpacing.x * 2.0f) * 0.5f;
  igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.10f, 0.45f, 0.28f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.14f, 0.58f, 0.36f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.08f, 0.36f, 0.22f, 1.0f});
  bool play = igButton("Play##play", (ImVec2){bwid, 0});
  igPopStyleColor(3);
  igSameLine(0, style->ItemSpacing.x);
  if (!can) igBeginDisabled(true);
  if (igButton(label, (ImVec2){bwid, 0})) lb_start(i);
  if (!can) igEndDisabled();
  igSameLine(0, style->ItemSpacing.x);
  if (rt->running) igBeginDisabled(true);
  bool remove_me = igButton("X##rm", (ImVec2){xw, 0});
  if (rt->running) igEndDisabled();

  if (s->error[0])
    igTextColored((ImVec4){1.0f, 0.45f, 0.40f, 1.0f}, "Last refresh failed: %s", s->error);

  igSpacing();
  {
    ImVec2 hp;
    igGetCursorScreenPos(&hp);
    igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
               usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
    char upd[48] = "";
    if (s->data_time) {
      char a[32];
      fmt_age(s->data_time, a, sizeof a);
      snprintf(upd, sizeof upd, "Updated %s", a);
    }
    ImVec2 ts;
    igCalcTextSize(&ts, upd, NULL, false, -1.0f);
    ImDrawList_AddText_Vec2(dl, (ImVec2){hp.x + inner_w - ts.x, hp.y + 2}, COL(130, 140, 170, 255), upd, NULL);
    igPopFont();
    igTextColored((ImVec4){0.86f, 0.90f, 1.0f, 1.0f}, "Leaderboard");
  }
  igSpacing();
  ImVec2 nsz;
  igCalcTextSize(&nsz, "#10", NULL, false, -1.0f);
  float rowh = nsz.y + 8.0f * scale;
  for (int k = 0; k < LB_TOP; k++) {
    ImVec2 rp;
    igGetCursorScreenPos(&rp);
    if (k % 2 == 0)
      ImDrawList_AddRectFilled(dl, rp, (ImVec2){rp.x + inner_w, rp.y + rowh}, COL(255, 255, 255, 14), 4.0f, 0);
    char num[8];
    snprintf(num, sizeof num, "#%d", k + 1);
    ImDrawList_AddText_Vec2(dl, (ImVec2){rp.x + 8, rp.y + 4.0f * scale}, COL(90, 200, 245, 255), num, NULL);
    if (k < s->top_count) {
      char sc[24];
      fmt_num(s->score[k], sc, sizeof sc);
      ImVec2 ss;
      igCalcTextSize(&ss, sc, NULL, false, -1.0f);
      char nm[MAX_NICKNAME_LEN + 1];
      snprintf(nm, sizeof nm, "%s", s->name[k]);
      float name_x = rp.x + 8 + nsz.x + 10.0f * scale;
      fit_text(nm, rp.x + inner_w - ss.x - 12.0f - name_x);
      ImDrawList_AddText_Vec2(dl, (ImVec2){name_x, rp.y + 4.0f * scale}, COL(235, 240, 250, 255), nm, NULL);
      ImDrawList_AddText_Vec2(dl, (ImVec2){rp.x + inner_w - ss.x - 8, rp.y + 4.0f * scale}, COL(255, 255, 255, 255), sc, NULL);
    }
    igDummy((ImVec2){inner_w, rowh});
  }

  igEndChild();
  igPopStyleColor(2);
  igPopID();
  if (play) lb_play(i);
  else if (remove_me) lb_remove(i);
}

void ui_leaderboard_panel(tenv* env) {
  if (!env || !env->usr) return;
  s_env = env;
  lb_load();
  tuser_data* usr = env->usr;
  ImGuiStyle* style = igGetStyle();
  const float scale = imgui_get_ui_scale();

  {
    ImGuiViewport* vp = igGetMainViewport();
    ImDrawList* bg = igGetBackgroundDrawList(vp);
    ImDrawList_AddRectFilled(bg, vp->Pos,
                             (ImVec2){vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y},
                             COL(0, 0, 0, 255), 0, 0);
    ImVec2 wp, ws;
    igGetWindowPos(&wp);
    igGetWindowSize(&ws);
    ImDrawList_AddRectFilled(igGetWindowDrawList(), wp,
                             (ImVec2){wp.x + ws.x, wp.y + ws.y}, COL(0, 0, 0, 255), 0, 0);
  }

  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]->LegacySize);
  igTextColored((ImVec4){0.48f, 0.72f, 1.0f, 1.0f}, "Leaderboard");

  int used = 0;
  int gw = global_wait(&used);
  char tw[16];
  if (gw > 0) {
    fmt_mmss(gw, tw, sizeof tw);
    igTextColored((ImVec4){1.0f, 0.70f, 0.35f, 1.0f},
                  "Limit reached. Next refresh in %s", tw);
  } else {
    igTextDisabled("Refreshes left: %d / %d (every server: once per 5 min)",
                   LB_GLOBAL_RUNS - used, LB_GLOBAL_RUNS);
  }

  ImVec2 avail;
  igGetContentRegionAvail(&avail);
  if (igButton("Back to homepage##lb", (ImVec2){avail.x, 0})) {
    usr->gdata.curr_screen = TITLE_SCREEN;
    igPopFont();
    return;
  }

  float add_w = 90.0f * scale;
  igSetNextItemWidth(avail.x - add_w - style->ItemSpacing.x);
  igInputTextWithHint("##lb_input", "Server IP:port or SID", s_input,
                      sizeof s_input, ImGuiInputTextFlags_None, NULL, NULL);
  igSameLine(0, style->ItemSpacing.x);
  if (igButton("Add##lb", (ImVec2){add_w, 0})) lb_add_from_input(env);
  if (s_message[0])
    igTextColored(s_message_bad ? (ImVec4){1.0f, 0.45f, 0.40f, 1.0f}
                                : (ImVec4){0.55f, 0.95f, 0.55f, 1.0f},
                  "%s", s_message);

  if (igCollapsingHeader_TreeNodeFlags("How to use", ImGuiTreeNodeFlags_DefaultOpen)) {
    igTextWrapped("1. Type a server IP:port (or its SID) and tap Add. Saved servers stay here.");
    igTextWrapped("2. Tap Start (first time) or Reload on a card. A hidden bot called \"Vlither android.bot\" joins that server, reads the leaderboard, player count and minimap, then leaves after a few seconds.");
    igTextWrapped("3. Timer: one server can be refreshed once every 5 minutes. The button shows Wait m:ss until it is ready again.");
    igTextWrapped("4. Limit: at most 5 refreshes can start in any 5 minutes, over all servers. The button then shows Limit m:ss, and the line at the top shows how many refreshes are left.");
    igTextWrapped("5. Servers with a CUSTOM badge have no timer and no limit.");
    igTextWrapped("6. Play connects you to that server. Cards are sorted by the best ping first.");
  }
  igSeparator();

  igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){0, 0, 0, 1});
  igBeginChild_Str("##lb_list", (ImVec2){0, 0}, ImGuiChildFlags_None,
                   ImGuiWindowFlags_AlwaysVerticalScrollbar);
  if (s_n == 0) igTextDisabled("No servers saved yet. Add one above.");

  ImVec2 la;
  igGetContentRegionAvail(&la);
  float sp = style->ItemSpacing.x;
  int cols = (int)((la.x + sp) / (300.0f * scale + sp));
  if (cols < 1) cols = 1;
  if (cols > 4) cols = 4;
  float card_w = (la.x - sp * (float)(cols - 1)) / (float)cols;
  int order[LB_MAX_SERVERS];
  for (int k = 0; k < s_n; k++) order[k] = k;
  for (int a = 1; a < s_n; a++) {
    int v = order[a];
    int pv = s_srv[v].ping_ms > 0 ? s_srv[v].ping_ms : 1 << 30;
    int b = a - 1;
    while (b >= 0 &&
           (s_srv[order[b]].ping_ms > 0 ? s_srv[order[b]].ping_ms : 1 << 30) > pv) {
      order[b + 1] = order[b];
      b--;
    }
    order[b + 1] = v;
  }
  for (int k = 0; k < s_n; k++) {
    int i = order[k];
    int before = s_n;
    if (k % cols != 0) igSameLine(0, sp);
    draw_card(i, scale, card_w, usr);
    if (s_n != before) break;
    if (usr->gdata.curr_screen != LEADERBOARD_PANEL) break;
  }
  igEndChild();
  igPopStyleColor(1);
  igPopFont();
}
