#include "ntl_team.h"
#include "ntl_tags.h"
#include "vlither_tags.h"
#include "../user.h"
#include "../network/server.h"
#include "../external/mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#ifdef ANDROID
#include "../android_glfw_shim.h"
#include "../android_jni.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|(ImU32)(R))
#endif
#define NTL_MAX_TEAM 64
#define NTL_URL_MAX 2048
/* Protocol version the NTL server expects (extension manifest version).
   Was 4.1; the extension now reports 9.68 and the server may gate on it. */
#define NTL_PROTOCOL_VER "9.68"
#define NTL_POLL_SECONDS 1.0
#define NTL_RETRY_BASE_SECONDS 1.0
#define NTL_RETRY_MAX_SECONDS 6.0
#define NTL_REQUEST_TIMEOUT_SECONDS 7.0
#define NTL_CONNECTED_GRACE_SECONDS 35.0
#define NTL_CHAT_HISTORY_MAX 80
#define NTL_CHAT_SNAPSHOT_MAX 1024

typedef struct {
  char nick[64], msg[NTL_CHAT_SNAPSHOT_MAX], srv[64], dt[128];
  char ver[16], owner[32], vlither_ver[16];
  float x, y;
  int sid, score, rank, tg;
  int fps, ping;
  bool is_bot, is_sos, has_telemetry, has_vlither_telemetry;
} ntl_member;
typedef struct {
  tenv *env;
  struct mg_mgr mgr;
  struct mg_connection *request_conn;
  bool ready, request_active;
  double next_poll, request_started, last_success;
  ntl_member members[NTL_MAX_TEAM]; int count;
  char pending_msg[256];
  char inflight_msg[256];
  /* NTL and Vlither chat visibility must be independent. A single shared
     flag made disabling one network's HUD silently disable the other. */
  char input[256];
  bool ntl_chat_open;
  bool vlither_chat_open;
  bool players_open;
  char request_path[NTL_URL_MAX]; int last_http_status; bool last_request_ok;
  int consecutive_failures;
  struct { char nick[64], text[256]; time_t time; } history[NTL_CHAT_HISTORY_MAX];
  int history_count, history_start;
  char profile_name[MAX_NTL_TEAM_NAME + 1];
  bool chat_restore_size;
  float chat_x, chat_y, chat_w, chat_h;
  float players_x, players_y, players_w, players_h;
  bool layout_dirty;
  struct {
    char key[96];
    char msg[NTL_CHAT_SNAPSHOT_MAX];
  } seen[NTL_MAX_TEAM];
  int seen_count; bool scroll_chat_bottom;
  bool feed_baselined;
  bool select_chat_tab;
  char active_team_id[96];
  char active_auth_key[96];
  char request_team_id[96];
  char request_auth_key[96];
  char request_client_id[9];
  char last_sent_text[256];
  double last_sent_time;
  char last_nickname[MAX_NICKNAME_LEN + 1];
  char last_presence_server[MAX_SERVER_IP_LEN + 1];
  bool last_presence_playing;
  bool vlither_chat_active;
  bool focus_vlither_input;
  int last_vlither_history_count;
  double sos_until;
  bool sos_message_pending;
} ntl_state;
static ntl_state S;

static float ntl_clampf(float v, float lo, float hi) {
  if (hi < lo) hi = lo;
  return v < lo ? lo : (v > hi ? hi : v);
}

static ImVec4 ntl_unique_color(const char *name, unsigned int salt) {
  unsigned int h = 2166136261u ^ salt;
  const unsigned char *p = (const unsigned char *)(name ? name : "Player");
  while (*p) { h ^= *p++; h *= 16777619u; }
  float hue = (float)(h % 360u) / 60.0f;
  float x = 0.28f + 0.72f * (1.0f - fabsf(fmodf(hue, 2.0f) - 1.0f));
  float r = 1.0f, g = x, b = 0.28f;
  switch ((int)hue) {
    case 0: r = 1.0f; g = x; b = 0.28f; break;
    case 1: r = x; g = 1.0f; b = 0.28f; break;
    case 2: r = 0.28f; g = 1.0f; b = x; break;
    case 3: r = 0.28f; g = x; b = 1.0f; break;
    case 4: r = x; g = 0.28f; b = 1.0f; break;
    default: r = 1.0f; g = 0.28f; b = x; break;
  }
  return (ImVec4){r, g, b, 1.0f};
}

static ImU32 ntl_unique_u32(const char *name, unsigned int salt) {
  return igColorConvertFloat4ToU32(ntl_unique_color(name, salt));
}

static int ntl_hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static ImVec4 ntl_vlither_profile_color(const char *hex,
                                        const char *fallback_name) {
  if (!hex || strlen(hex) != 6)
    return ntl_unique_color(fallback_name, 0x564c4954u);
  int rgb[3];
  for (int i = 0; i < 3; ++i) {
    int hi = ntl_hex_digit(hex[i * 2]);
    int lo = ntl_hex_digit(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0)
      return ntl_unique_color(fallback_name, 0x564c4954u);
    rgb[i] = hi * 16 + lo;
  }
  return (ImVec4){rgb[0] / 255.0f, rgb[1] / 255.0f,
                  rgb[2] / 255.0f, 1.0f};
}

static ImVec4 ntl_vlither_text_color(const char *hex,
                                     const char *fallback_name) {
  ImVec4 color = ntl_vlither_profile_color(hex, fallback_name);
  float luma = color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
  if (luma < 0.34f) {
    float mix = (0.34f - luma) / 0.34f * 0.55f;
    color.x += (1.0f - color.x) * mix;
    color.y += (1.0f - color.y) * mix;
    color.z += (1.0f - color.z) * mix;
  }
  return color;
}

static void ntl_vlither_profile_label(char *out, size_t cap,
                                      const char *emoji, const char *name) {
  if (!out || cap == 0) return;
  if (!name || !name[0]) name = "Vlither";
  if (emoji && emoji[0]) snprintf(out, cap, "%s %s", emoji, name);
  else snprintf(out, cap, "%s", name);
  out[cap - 1] = 0;
}
static bool ntl_feed_is_fresh(void);
static void ntl_queue_message(const user_settings *us);
static void chat_submit_current(const user_settings *us);
static const char *ntl_clean_name(const char *name);
static void ntl_reset_feed(bool clear_history);
static void ntl_ensure_client_id(user_settings *us);
static void ntl_schedule_retry(double now);
static bool same_server(const char *a, const char *b);
static size_t ntl_append_utf8(char *out, size_t cap, size_t n,
                              unsigned int cp);

static void ntl_history_clock(time_t when, char out[6]) {
  out[0] = 0;
  struct tm *local = localtime(&when);
  if (local) strftime(out, 6, "%H:%M", local);
}

static void urlenc(char *out,size_t cap,const char *in){size_t n=0;for(;*in&&n+4<cap;in++){unsigned char c=*in;if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out[n++]=c;else{snprintf(out+n,cap-n,"%%%02X",c);n+=3;}}out[n]=0;}
static int ntl_hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

static bool field_str(const char *a, const char *b, const char *key,
                      char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;

  char pattern[64];
  snprintf(pattern, sizeof pattern, "\"%s\"", key);
  const char *q = strstr(a, pattern);
  if (!q || q >= b) return false;

  q = strchr(q, ':');
  if (!q || q >= b) return false;
  q++;
  while (q < b && isspace((unsigned char)*q)) q++;
  if (q >= b || *q != '"') return false;
  q++;

  size_t n = 0;
  while (q < b && *q != '"') {
    if (*q != '\\') {
      if (n + 1 >= cap) break;
      out[n++] = *q++;
      continue;
    }

    q++;
    if (q >= b) break;
    char esc = *q++;

    if (esc == 'u') {
      if (q + 4 > b) continue;
      int h0 = ntl_hex_value(q[0]);
      int h1 = ntl_hex_value(q[1]);
      int h2 = ntl_hex_value(q[2]);
      int h3 = ntl_hex_value(q[3]);
      if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) continue;

      unsigned int cp =
          (unsigned int)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
      q += 4;

      if (cp >= 0xd800 && cp <= 0xdbff && q + 6 <= b &&
          q[0] == '\\' && q[1] == 'u') {
        int l0 = ntl_hex_value(q[2]);
        int l1 = ntl_hex_value(q[3]);
        int l2 = ntl_hex_value(q[4]);
        int l3 = ntl_hex_value(q[5]);
        if (l0 >= 0 && l1 >= 0 && l2 >= 0 && l3 >= 0) {
          unsigned int low =
              (unsigned int)((l0 << 12) | (l1 << 8) | (l2 << 4) | l3);
          if (low >= 0xdc00 && low <= 0xdfff) {
            cp = 0x10000u + ((cp - 0xd800u) << 10) + (low - 0xdc00u);
            q += 6;
          }
        }
      }

      size_t next = ntl_append_utf8(out, cap, n, cp);
      if (next == n) break;
      n = next;
      continue;
    }

    char decoded = esc;
    switch (esc) {
      case '"': decoded = '"'; break;
      case '\\': decoded = '\\'; break;
      case '/': decoded = '/'; break;
      case 'b': decoded = '\b'; break;
      case 'f': decoded = '\f'; break;
      case 'n': decoded = '\n'; break;
      case 'r': decoded = '\r'; break;
      case 't': decoded = '\t'; break;
      default: break;
    }
    if (n + 1 >= cap) break;
    out[n++] = decoded;
  }

  out[n] = 0;
  return true;
}
static double field_num(const char *a, const char *b, const char *key, double fallback) {
  char pattern[64];
  snprintf(pattern, sizeof pattern, "\"%s\"", key);

  const char *q = strstr(a, pattern);
  if (!q || q >= b) return fallback;

  q = strchr(q, ':');
  if (!q || q >= b) return fallback;
  q++;

  while (q < b && isspace((unsigned char)*q)) q++;
  if (q >= b) return fallback;

  /* The endpoint can serialize numeric fields as JSON strings. Accept both
     normal JSON numbers and quoted numeric strings. */
  if (*q == '\"') {
    q++;
    while (q < b && isspace((unsigned char)*q)) q++;
  }

  char *endptr = NULL;
  double value = strtod(q, &endptr);
  if (endptr == q || endptr > b) return fallback;
  return value;
}

static bool field_bool(const char *a, const char *b, const char *key,
                       bool fallback) {
  char pattern[64];
  snprintf(pattern, sizeof pattern, "\"%s\"", key);
  const char *q = strstr(a, pattern);
  if (!q || q >= b) return fallback;
  q = strchr(q, ':');
  if (!q || q >= b) return fallback;
  q++;
  while (q < b && isspace((unsigned char)*q)) q++;
  if (q >= b) return fallback;
  if (*q == '"') q++;
  if (q + 4 <= b && !strncmp(q, "true", 4)) return true;
  if (q + 5 <= b && !strncmp(q, "false", 5)) return false;
  if (*q == '1') return true;
  if (*q == '0') return false;
  return fallback;
}
static void ntl_parse_vlither_telemetry(ntl_member *m) {
  if (!m) return;
  m->fps = -1;
  m->ping = -1;
  m->vlither_ver[0] = 0;
  m->has_telemetry = false;
  m->has_vlither_telemetry = false;

  /* Current Vlither Android publishes the same readable FPS/ping prefix used
     by the official NTL client, followed by its Vlither version. This lets
     normal NTL clients display the line too. Keep support for the short-lived
     semicolon format from earlier Vlither Android builds as well. */
  if (strncmp(m->dt, "VlitherAndroid;", 15) == 0) {
    const char *v = strstr(m->dt, ";v=");
    const char *fps = strstr(m->dt, ";fps=");
    const char *ping = strstr(m->dt, ";ping=");
    if (v) {
      v += 3;
      size_t n = 0;
      while (v[n] && v[n] != ';' && n + 1 < sizeof m->vlither_ver) {
        m->vlither_ver[n] = v[n];
        ++n;
      }
      m->vlither_ver[n] = 0;
    }
    if (fps) {
      long value = strtol(fps + 5, NULL, 10);
      if (value >= 0 && value <= 1000) m->fps = (int)value;
    }
    if (ping) {
      long value = strtol(ping + 6, NULL, 10);
      if (value >= 0 && value <= 60000) m->ping = (int)value;
    }
    m->has_telemetry = m->fps >= 0 || m->ping >= 0;
    m->has_vlither_telemetry = true;
    return;
  }

  const char *version = strstr(m->dt, "Vlither v");
  if (version) {
    version += strlen("Vlither v");
    size_t n = 0;
    while (version[n] && !isspace((unsigned char)version[n]) &&
           version[n] != '|' && version[n] != ';' &&
           n + 1 < sizeof m->vlither_ver) {
      m->vlither_ver[n] = version[n];
      ++n;
    }
    m->vlither_ver[n] = 0;
    m->has_vlither_telemetry = true;
  }

  /* Official NTL detail strings use: FPS: <fps> @ <ping>(<avg>) ms @ ...
     Parse that format for every NTL member, not only Vlither clients. */
  const char *fps_label = strstr(m->dt, "FPS:");
  if (fps_label) {
    char *endptr = NULL;
    long value = strtol(fps_label + 4, &endptr, 10);
    if (endptr != fps_label + 4 && value >= 0 && value <= 1000)
      m->fps = (int)value;
    const char *at = strchr(endptr ? endptr : fps_label + 4, '@');
    if (at) {
      long pvalue = strtol(at + 1, &endptr, 10);
      if (endptr != at + 1 && pvalue >= 0 && pvalue <= 60000)
        m->ping = (int)pvalue;
    }
  }
  m->has_telemetry = m->fps >= 0 || m->ping >= 0;
}

static size_t ntl_append_utf8(char *out, size_t cap, size_t n,
                              unsigned int cp) {
  if (cp <= 0x7f) {
    if (n + 1 < cap) out[n++] = (char)cp;
  } else if (cp <= 0x7ff) {
    if (n + 2 < cap) {
      out[n++] = (char)(0xc0 | (cp >> 6));
      out[n++] = (char)(0x80 | (cp & 0x3f));
    }
  } else if (cp <= 0xffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
    if (n + 3 < cap) {
      out[n++] = (char)(0xe0 | (cp >> 12));
      out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[n++] = (char)(0x80 | (cp & 0x3f));
    }
  } else if (cp <= 0x10ffff) {
    if (n + 4 < cap) {
      out[n++] = (char)(0xf0 | (cp >> 18));
      out[n++] = (char)(0x80 | ((cp >> 12) & 0x3f));
      out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[n++] = (char)(0x80 | (cp & 0x3f));
    }
  }
  return n;
}

/* NTL stores a browser-style message snapshot per client. Keep line breaks
   while decoding so a snapshot containing several queued messages can be
   compared with the previous snapshot without losing any intermediate text. */
static void ntl_decode_chat_snapshot(char *text, size_t cap) {
  if (!text || cap == 0) return;

  char out[NTL_CHAT_SNAPSHOT_MAX];
  size_t n = 0;
  bool last_space = true;
  const char *p = text;

  while (*p && n + 1 < sizeof out) {
    unsigned int cp = 0;
    size_t consumed = 0;
    bool line_break = false;

    if (!strncmp(p, "&nbsp;", 6)) { cp = ' '; consumed = 6; }
    else if (!strncmp(p, "&nbsp", 5)) { cp = ' '; consumed = 5; }
    else if (!strncmp(p, "&#160;", 6)) { cp = ' '; consumed = 6; }
    else if (!strncmp(p, "&amp;", 5)) { cp = '&'; consumed = 5; }
    else if (!strncmp(p, "&lt;", 4)) { cp = '<'; consumed = 4; }
    else if (!strncmp(p, "&gt;", 4)) { cp = '>'; consumed = 4; }
    else if (!strncmp(p, "&quot;", 6)) { cp = '"'; consumed = 6; }
    else if (!strncmp(p, "&#39;", 5) || !strncmp(p, "&apos;", 6)) {
      cp = '\'';
      consumed = p[1] == '#' ? 5 : 6;
    } else if (!strncmp(p, "<br>", 4) || !strncmp(p, "<BR>", 4)) {
      consumed = 4;
      line_break = true;
    } else if (!strncmp(p, "<br/>", 5) || !strncmp(p, "<BR/>", 5)) {
      consumed = 5;
      line_break = true;
    } else if (!strncmp(p, "<br />", 6) || !strncmp(p, "<BR />", 6)) {
      consumed = 6;
      line_break = true;
    } else if (p[0] == '&' && p[1] == '#') {
      const char *q = p + 2;
      int base = 10;
      if (*q == 'x' || *q == 'X') { base = 16; q++; }
      char *endptr = NULL;
      unsigned long value = strtoul(q, &endptr, base);
      if (endptr != q && *endptr == ';' && value <= 0x10ffffUL) {
        cp = (unsigned int)value;
        consumed = (size_t)(endptr - p) + 1;
      }
    }

    if (consumed) {
      p += consumed;
      if (line_break) {
        while (n > 0 && out[n - 1] == ' ') n--;
        if (n > 0 && out[n - 1] != '\n' && n + 1 < sizeof out)
          out[n++] = '\n';
        last_space = true;
        continue;
      }
      if (cp == 0xa0 || cp == '\r' || cp == '\t') cp = ' ';
      if (cp == '\n') {
        while (n > 0 && out[n - 1] == ' ') n--;
        if (n > 0 && out[n - 1] != '\n' && n + 1 < sizeof out)
          out[n++] = '\n';
        last_space = true;
      } else if (cp == ' ') {
        if (!last_space && n + 1 < sizeof out) out[n++] = ' ';
        last_space = true;
      } else {
        size_t next = ntl_append_utf8(out, sizeof out, n, cp);
        if (next == n) break;
        n = next;
        last_space = false;
      }
      continue;
    }

    unsigned char c = (unsigned char)*p++;
    if (c == '\n') {
      while (n > 0 && out[n - 1] == ' ') n--;
      if (n > 0 && out[n - 1] != '\n' && n + 1 < sizeof out)
        out[n++] = '\n';
      last_space = true;
    } else if (c == '\r' || c == '\t' || c == ' ') {
      if (!last_space && n + 1 < sizeof out) out[n++] = ' ';
      last_space = true;
    } else {
      out[n++] = (char)c;
      last_space = false;
    }
  }

  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n')) n--;
  out[n] = 0;
  strncpy(text, out, cap - 1);
  text[cap - 1] = 0;
}

static void ntl_normalize_chat_text(char *text, size_t cap) {
  if (!text || cap == 0) return;

  char decoded[NTL_CHAT_SNAPSHOT_MAX];
  strncpy(decoded, text, sizeof decoded - 1);
  decoded[sizeof decoded - 1] = 0;
  ntl_decode_chat_snapshot(decoded, sizeof decoded);

  size_t n = 0;
  bool last_space = true;
  for (const unsigned char *p = (const unsigned char *)decoded;
       *p && n + 1 < cap; ++p) {
    if (*p == '\n' || *p == '\r' || *p == '\t' || *p == ' ') {
      if (!last_space) text[n++] = ' ';
      last_space = true;
    } else {
      text[n++] = (char)*p;
      last_space = false;
    }
  }
  while (n > 0 && text[n - 1] == ' ') n--;
  text[n] = 0;
}

static void add_history(const char *nick, const char *text) {
  if (!text || !text[0]) return;

  char clean_text[256];
  strncpy(clean_text, text, sizeof clean_text - 1);
  clean_text[sizeof clean_text - 1] = 0;
  ntl_normalize_chat_text(clean_text, sizeof clean_text);
  if (!clean_text[0]) return;

  char clean_nick[64];
  const char *display_name = ntl_clean_name(nick);
  strncpy(clean_nick, display_name && display_name[0] ? display_name : "Player",
          sizeof clean_nick - 1);
  clean_nick[sizeof clean_nick - 1] = 0;
  ntl_normalize_chat_text(clean_nick, sizeof clean_nick);
  if (!clean_nick[0]) strcpy(clean_nick, "Player");

  int idx;
  if (S.history_count < NTL_CHAT_HISTORY_MAX) {
    idx = (S.history_start + S.history_count) % NTL_CHAT_HISTORY_MAX;
    S.history_count++;
  } else {
    idx = S.history_start;
    S.history_start = (S.history_start + 1) % NTL_CHAT_HISTORY_MAX;
  }
  strncpy(S.history[idx].nick, clean_nick, sizeof S.history[idx].nick - 1);
  S.history[idx].nick[sizeof S.history[idx].nick - 1] = 0;
  strncpy(S.history[idx].text, clean_text, sizeof S.history[idx].text - 1);
  S.history[idx].text[sizeof S.history[idx].text - 1] = 0;
  S.history[idx].time = time(NULL);
  S.scroll_chat_bottom = true;
}

void ntl_team_system_message(const char *text) {
  if (text && text[0]) add_history("SCRIPTBOT", text);
}

bool ntl_team_send_text(const char *text) {
  if (!S.ready || !S.env || !S.env->usr || !text || !text[0]) return false;
  user_settings *us = &S.env->usr->usrs;
  if (!us->ntl_enabled || strlen(us->ntl_auth_key) < 16 ||
      strlen(us->ntl_team_id) < 16) {
    ntl_team_system_message(
        "NTL chat is not configured. Add your team ID and auth key first.");
    S.ntl_chat_open = true;
    us->ntl_chat_hud_visible = true;
    save_user_settings(us);
    return false;
  }

  strncpy(S.input, text, sizeof S.input - 1);
  S.input[sizeof S.input - 1] = 0;
  S.ntl_chat_open = true;
  us->ntl_chat_hud_visible = true;
  us->ntl_chat_minimized = false;
  save_user_settings(us);
  ntl_queue_message(us);
  return true;
}

int ntl_team_tag_for_snake(uint16_t ntl_id, const char *server) {
  if (!S.ready || !server || !server[0]) return -1;
  for (int i = 0; i < S.count; ++i) {
    ntl_member *m = &S.members[i];
    if (m->sid == (int)ntl_id && same_server(m->srv, server) &&
        m->tg >= 0 && m->tg <= 666)
      return m->tg;
  }
  return -1;
}

bool ntl_team_is_snake_teammate(uint16_t ntl_id, const char *server) {
  if (!S.ready || !S.env || !S.env->usr || !server || !server[0])
    return false;
  user_settings *us = &S.env->usr->usrs;
  if (!us->ntl_enabled || !ntl_feed_is_fresh()) return false;
  for (int i = 0; i < S.count; ++i) {
    ntl_member *m = &S.members[i];
    if (m->sid == (int)ntl_id && same_server(m->srv, server)) return true;
  }
  return false;
}


static void ntl_message_key(const char *nick, char *out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (!nick) return;

  bool has_client_id = strlen(nick) >= 8;
  for (int i = 0; has_client_id && i < 8; ++i)
    has_client_id = isxdigit((unsigned char)nick[i]) != 0;

  if (has_client_id) {
    size_t n = cap > 9 ? 8 : cap - 1;
    for (size_t i = 0; i < n; ++i)
      out[i] = (char)tolower((unsigned char)nick[i]);
    out[n] = 0;
  } else {
    strncpy(out, nick, cap - 1);
    out[cap - 1] = 0;
  }
}

static int seen_index_for(const char *nick) {
  char key[96];
  ntl_message_key(nick, key, sizeof key);
  for (int i = 0; i < S.seen_count; ++i)
    if (!strcmp(S.seen[i].key, key)) return i;
  return -1;
}

static const char *seen_message_for(const char *nick) {
  int index = seen_index_for(nick);
  return index >= 0 ? S.seen[index].msg : NULL;
}

static void remember_message(const char *nick, const char *msg) {
  char key[96];
  ntl_message_key(nick, key, sizeof key);
  int index = seen_index_for(nick);

  if (index < 0) {
    if (S.seen_count >= NTL_MAX_TEAM) return;
    index = S.seen_count++;
    strncpy(S.seen[index].key, key, sizeof S.seen[index].key - 1);
    S.seen[index].key[sizeof S.seen[index].key - 1] = 0;
  }

  strncpy(S.seen[index].msg, msg ? msg : "", sizeof S.seen[index].msg - 1);
  S.seen[index].msg[sizeof S.seen[index].msg - 1] = 0;
}

static size_t ntl_snapshot_overlap(const char *old_msg, const char *new_msg) {
  if (!old_msg || !new_msg) return 0;
  size_t old_len = strlen(old_msg);
  size_t new_len = strlen(new_msg);
  size_t best = 0;

  /* Match complete message records only. A raw character suffix match could
     turn "hello" followed by "okay" into "kay" because both touch on "o". */
  for (size_t start = 0; start < old_len; ++start) {
    if (start > 0 && old_msg[start - 1] != '\n') continue;
    size_t overlap = old_len - start;
    if (overlap <= best || overlap > new_len) continue;
    if (new_msg[overlap] != 0 && new_msg[overlap] != '\n') continue;
    if (!memcmp(old_msg + start, new_msg, overlap)) best = overlap;
  }
  return best;
}

static void ntl_add_snapshot_delta(const char *nick, const char *delta) {
  if (!delta) return;
  while (*delta == '\n' || *delta == ' ') delta++;

  const char *p = delta;
  while (*p) {
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);

    /* The desktop Vlither reference joins a burst using " | ". Accept that
       separator too, while the official extension normally uses <br>. */
    const char *segment = p;
    const char *segment_end = p + len;
    while (segment < segment_end) {
      const char *pipe = NULL;
      for (const char *q = segment; q + 2 < segment_end; ++q) {
        if (q[0] == ' ' && q[1] == '|' && q[2] == ' ') {
          pipe = q;
          break;
        }
      }

      const char *part_end = pipe ? pipe : segment_end;
      while (segment < part_end && isspace((unsigned char)*segment)) segment++;
      while (part_end > segment &&
             isspace((unsigned char)part_end[-1])) part_end--;

      if (part_end > segment) {
        char message[256];
        size_t part_len = (size_t)(part_end - segment);
        if (part_len >= sizeof message) part_len = sizeof message - 1;
        memcpy(message, segment, part_len);
        message[part_len] = 0;
        add_history(nick, message);
      }

      if (!pipe) break;
      segment = pipe + 3;
    }

    if (!end) break;
    p = end + 1;
  }
}

static bool ntl_snapshot_contains_message(const char *snapshot,
                                          const char *message) {
  if (!snapshot || !message || !message[0]) return false;

  char target[256];
  strncpy(target, message, sizeof target - 1);
  target[sizeof target - 1] = 0;
  ntl_normalize_chat_text(target, sizeof target);

  const char *p = snapshot;
  while (*p) {
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char part[256];
    if (len >= sizeof part) len = sizeof part - 1;
    memcpy(part, p, len);
    part[len] = 0;
    ntl_normalize_chat_text(part, sizeof part);
    if (!strcmp(part, target)) return true;
    if (!end) break;
    p = end + 1;
  }
  return false;
}

static bool ntl_client_id_is_valid(const char *id) {
  if (!id || strlen(id) != 8) return false;
  for (int i = 0; i < 8; ++i) {
    char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

static void ntl_ensure_client_id(user_settings *us) {
  if (!us || ntl_client_id_is_valid(us->ntl_client_id)) return;

  unsigned char random_bytes[4] = {0};
  if (!mg_random(random_bytes, sizeof random_bytes)) {
    uint64_t fallback = mg_millis() ^ (uintptr_t)us;
    for (int i = 0; i < 4; ++i)
      random_bytes[i] = (unsigned char)(fallback >> (i * 8));
  }
  snprintf(us->ntl_client_id, sizeof us->ntl_client_id, "%02x%02x%02x%02x",
           random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3]);
  save_user_settings(us);
}

static double ntl_retry_delay(void) {
  int step = S.consecutive_failures > 0 ? S.consecutive_failures - 1 : 0;
  if (step > 3) step = 3;
  double delay = NTL_RETRY_BASE_SECONDS * (double)(1 << step);
  return delay > NTL_RETRY_MAX_SECONDS ? NTL_RETRY_MAX_SECONDS : delay;
}

static void ntl_schedule_retry(double now) {
  if (S.consecutive_failures < 1000) S.consecutive_failures++;
  S.last_request_ok = false;
  S.next_poll = now + ntl_retry_delay();
}

static void ntl_reset_feed(bool clear_history) {
  memset(S.members, 0, sizeof S.members);
  S.count = 0;
  memset(S.seen, 0, sizeof S.seen);
  S.seen_count = 0;
  S.feed_baselined = false;
  S.last_success = 0.0;
  S.last_request_ok = false;
  S.last_http_status = 0;
  S.select_chat_tab = true;
  S.pending_msg[0] = 0;
  S.inflight_msg[0] = 0;
  S.consecutive_failures = 0;
  S.last_sent_text[0] = 0;
  S.last_sent_time = 0.0;
  if (clear_history) {
    memset(S.history, 0, sizeof S.history);
    S.history_count = 0;
    S.history_start = 0;
    S.scroll_chat_bottom = false;
  }
}

static void ntl_sync_active_credentials(const user_settings *us) {
  if (!us) return;
  if (!strcmp(S.active_team_id, us->ntl_team_id) &&
      !strcmp(S.active_auth_key, us->ntl_auth_key))
    return;

  ntl_reset_feed(true);
  strncpy(S.active_team_id, us->ntl_team_id, sizeof S.active_team_id - 1);
  S.active_team_id[sizeof S.active_team_id - 1] = 0;
  strncpy(S.active_auth_key, us->ntl_auth_key, sizeof S.active_auth_key - 1);
  S.active_auth_key[sizeof S.active_auth_key - 1] = 0;
}

static const char *ntl_find_object_end(const char *start,
                                       const char *document_end) {
  int depth = 0;
  bool in_string = false;
  bool escaped = false;

  for (const char *p = start; p < document_end; ++p) {
    char c = *p;
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }

    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0) return p;
    }
  }
  return NULL;
}

static bool parse_members(const char *s, size_t len) {
  ntl_member next[NTL_MAX_TEAM];
  int next_count = 0;
  bool baseline_only = !S.feed_baselined;
  bool own_message_echoed = !S.inflight_msg[0];
  memset(next, 0, sizeof next);

  const char *end = s + len;
  const char *p = s;
  while (next_count < NTL_MAX_TEAM && (p = strchr(p, '{')) && p < end) {
    const char *e = ntl_find_object_end(p, end);
    if (!e || e >= end) break;

    ntl_member *m = &next[next_count];
    memset(m, 0, sizeof *m);
    m->sid = -1;
    if (field_str(p, e, "nick", m->nick, sizeof m->nick)) {
      field_str(p, e, "msg", m->msg, sizeof m->msg);
      field_str(p, e, "srv", m->srv, sizeof m->srv);
      field_str(p, e, "dt", m->dt, sizeof m->dt);
      field_str(p, e, "ver", m->ver, sizeof m->ver);
      field_str(p, e, "owner", m->owner, sizeof m->owner);
      ntl_normalize_chat_text(m->nick, sizeof m->nick);
      ntl_decode_chat_snapshot(m->msg, sizeof m->msg);
      ntl_normalize_chat_text(m->dt, sizeof m->dt);
      ntl_normalize_chat_text(m->owner, sizeof m->owner);
      ntl_parse_vlither_telemetry(m);
      m->x = (float)field_num(p, e, "valx", 0);
      m->y = (float)field_num(p, e, "valy", 0);
      m->sid = (int)field_num(p, e, "sid", -1);
      m->score = (int)field_num(p, e, "score", 0);
      m->rank = (int)field_num(p, e, "rank", 0);
      m->tg = (int)field_num(p, e, "tg", -1);
      m->is_bot = field_bool(p, e, "bot", false);
      m->is_sos = field_bool(p, e, "sos", false);

      if (S.inflight_msg[0] && strlen(m->nick) >= 8 &&
          !strncmp(m->nick, S.request_client_id, 8) &&
          ntl_snapshot_contains_message(m->msg, S.inflight_msg))
        own_message_echoed = true;

      const char *old = seen_message_for(m->nick);
      if (!baseline_only && m->msg[0]) {
        if (!old || !old[0]) {
          /* A player first appearing after the initial baseline may already
             carry a real message. The extension displays it immediately. */
          ntl_add_snapshot_delta(m->nick, m->msg);
        } else if (strcmp(old, m->msg) != 0) {
          /* NTL can return an accumulated browser message buffer. Match the
             official extension by removing the previous snapshot prefix. The
             suffix/prefix overlap also handles a bounded server-side buffer
             that rotates older text out. */
          size_t overlap = ntl_snapshot_overlap(old, m->msg);
          ntl_add_snapshot_delta(m->nick, m->msg + overlap);
        }
      }
      remember_message(m->nick, m->msg);
      next_count++;
    }
    p = e + 1;
  }

  memcpy(S.members, next, sizeof next);
  S.count = next_count;
  S.feed_baselined = true;
  return own_message_echoed;
}

static void cb(struct mg_connection *c, int ev, void *ev_data) {
  /* Ignore final events from a request that has already timed out or has been
     superseded. This prevents an old close/error event from breaking a newer
     poll. */
  if (c != S.request_conn) return;

  if (ev == MG_EV_CONNECT) {
    /* FIX: send SNI. Without .name the TLS ClientHello has no server_name, so
       a vhost/CDN-fronted ntl-slither.com answers with the wrong cert or
       drops the handshake -> chat never connects. Every other NTL request
       (ntl_tags.c) already sets it. */
    struct mg_tls_opts tls = {.name = mg_str("ntl-slither.com"),
                              .skip_verification = 1};
    mg_tls_init(c, &tls);
    /* FIX: look like the extension's XHR (browser UA + slither.io origin).
       Some front-ends reject unknown non-browser user agents with 403. */
    mg_printf(c,
              "GET %s HTTP/1.1\r\nHost: ntl-slither.com\r\n"
              "User-Agent: Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 "
              "(KHTML, like Gecko) Chrome/131.0 Mobile Safari/537.36\r\n"
              "Origin: https://slither.io\r\n"
              "Referer: https://slither.io/\r\n"
              "Accept: */*\r\n"
              "Connection: close\r\n\r\n",
              S.request_path);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = ev_data;
    double now = mg_millis() / 1000.0;
    S.last_http_status = mg_http_status(hm);
    bool request_matches_active =
        !strcmp(S.request_team_id, S.active_team_id) &&
        !strcmp(S.request_auth_key, S.active_auth_key);
    S.last_request_ok = S.last_http_status == 200 && request_matches_active;

    if (S.last_request_ok) {
      size_t body_len = hm->body.len;
      char *body = malloc(body_len + 1);
      if (body) {
        memcpy(body, hm->body.buf, body_len);
        body[body_len] = 0;
        bool message_echoed = parse_members(body, body_len);
        free(body);
        S.last_success = now;
        S.consecutive_failures = 0;
        /* Match the extension protocol: keep publishing a chat message until
           this same eight-character client ID is returned with that message.
           This prevents a short connection interruption from producing only a
           local echo that other NTL clients never see. */
        if (message_echoed) S.inflight_msg[0] = 0;
        S.next_poll = now + NTL_POLL_SECONDS;
      } else {
        ntl_schedule_retry(now);
      }
    } else {
      ntl_schedule_retry(now);
    }

    S.request_active = false;
    S.request_conn = NULL;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR) {
    double now = mg_millis() / 1000.0;
    S.request_active = false;
    S.request_conn = NULL;
    ntl_schedule_retry(now);
  } else if (ev == MG_EV_CLOSE) {
    /* A close before an HTTP message is a failed heartbeat. Normal closes are
       ignored because MG_EV_HTTP_MSG already clears request_conn. */
    double now = mg_millis() / 1000.0;
    S.request_active = false;
    S.request_conn = NULL;
    ntl_schedule_retry(now);
  }
}
static snake *local_snake(game_data *g) {
  int count = tdarray_length(g->data.snakes);
  for (int i = 0; i < count; i++) {
    if (g->data.snakes[i].id == g->data.snake_id) return &g->data.snakes[i];
  }
  return NULL;
}

static void ntl_poll_request(tenv *env) {
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;

  if (S.request_active) return;
  ntl_sync_active_credentials(us);
  if (!us->ntl_enabled || strlen(us->ntl_auth_key) < 16 ||
      strlen(us->ntl_team_id) < 16) {
    return;
  }

  ntl_ensure_client_id(us);

  char raw_nick[sizeof us->ntl_client_id + MAX_NICKNAME_LEN + 2];
  const char *visible_nick = us->nickname[0] ? us->nickname : "Vlither";
  snprintf(raw_nick, sizeof raw_nick, "%s%s", us->ntl_client_id, visible_nick);

  bool in_game = g->conn == CONNECTED && g->curr_screen == PLAYING;
  snake *local = in_game ? local_snake(g) : NULL;
  const char *presence_server = local ? us->server_address : "_GAME_MENU_";
  /* Match the official NTL client: publish the authoritative world
     coordinates, not the short-lived render correction offsets (fx/fy).
     Sending fx/fy can make the team marker jump ahead/behind after packets. */
  float x = local ? local->xx : 0.0f;
  float y = local ? local->yy : 0.0f;
  int sid = local ? (int)local->ntl_id : -1;
  int score = local ? g->data.score : 0;
  int rank = local ? g->data.rank : 0;
  int fps = local && g->data.fps > 0 ? g->data.fps : -1;
  int ping = local && g->data.ping > 0 ? g->data.ping : -1;
  int ping_peak = local && g->data.ping_peak > 0 ? g->data.ping_peak : ping;

  /* Keep one message in flight until a successful response. A newly typed
     message can wait in pending_msg without overwriting the retrying one. */
  if (!S.inflight_msg[0] && S.sos_message_pending) {
    strncpy(S.inflight_msg, "Help me!", sizeof S.inflight_msg - 1);
    S.inflight_msg[sizeof S.inflight_msg - 1] = 0;
    S.sos_message_pending = false;
  } else if (!S.inflight_msg[0] && S.pending_msg[0]) {
    strncpy(S.inflight_msg, S.pending_msg, sizeof S.inflight_msg - 1);
    S.inflight_msg[sizeof S.inflight_msg - 1] = 0;
    S.pending_msg[0] = 0;
  }

  char nick[192];
  char msg[768];
  char srv[192];
  char dt_raw[128];
  char dt[256];
  if (fps >= 0 && ping >= 0)
    snprintf(dt_raw, sizeof dt_raw, "FPS: %d @ %d(%d) ms @ Vlither v%s",
             fps, ping, ping_peak, APP_VERSION);
  else
    snprintf(dt_raw, sizeof dt_raw, "Vlither v%s", APP_VERSION);
  urlenc(nick, sizeof nick, raw_nick);
  urlenc(msg, sizeof msg, S.inflight_msg);
  urlenc(srv, sizeof srv, presence_server);
  urlenc(dt, sizeof dt, dt_raw);

  strncpy(S.request_team_id, us->ntl_team_id, sizeof S.request_team_id - 1);
  S.request_team_id[sizeof S.request_team_id - 1] = 0;
  strncpy(S.request_auth_key, us->ntl_auth_key, sizeof S.request_auth_key - 1);
  S.request_auth_key[sizeof S.request_auth_key - 1] = 0;
  strncpy(S.request_client_id, us->ntl_client_id, sizeof S.request_client_id - 1);
  S.request_client_id[sizeof S.request_client_id - 1] = 0;

  snprintf(
      S.request_path, sizeof S.request_path,
      "/slither/ntlplay-mt.php?auth=%s&tid=%s&nick=%s&score=%d"
      "&valx=%.0f&valy=%.0f&bot=false&sos=%s&food=false&srv=%s"
      "&sid=%d&msg=%s&rank=%d&an=false&dt=%s&cs=%d&tg=%d&ver=" NTL_PROTOCOL_VER "&tlm=&di=1000",
      us->ntl_auth_key, us->ntl_team_id, nick, score, x, y,
      (S.sos_until > mg_millis() / 1000.0) ? "true" : "false", srv, sid,
      msg, rank, dt, local ? local->accessory : 0, us->ntl_tag_id);

  S.request_conn =
      mg_http_connect(&S.mgr, "https://ntl-slither.com", cb, NULL);
  S.request_active = S.request_conn != NULL;
  S.request_started = mg_millis() / 1000.0;
  if (!S.request_active) ntl_schedule_retry(S.request_started);
}
void ntl_team_init(tenv *env) {
  memset(&S, 0, sizeof S);
  S.env = env;
  mg_mgr_init(&S.mgr);
  S.ready = true;
  if (env && env->usr) {
    user_settings *us = &env->usr->usrs;
    ntl_ensure_client_id(us);
    strncpy(S.last_nickname, us->nickname, sizeof S.last_nickname - 1);
    S.last_nickname[sizeof S.last_nickname - 1] = 0;
    strncpy(S.last_presence_server, "_GAME_MENU_",
            sizeof S.last_presence_server - 1);
  }
  S.ntl_chat_open = env && env->usr
                        ? env->usr->usrs.ntl_chat_hud_visible : true;
  S.vlither_chat_open = env && env->usr
                            ? env->usr->usrs.vlither_chat_hud_visible : true;
  S.select_chat_tab = true;
  tuser_data *u = env ? env->usr : NULL;
  if (u && u->usrs.ntl_active_team_profile >= 0 &&
      u->usrs.ntl_active_team_profile < u->usrs.ntl_team_profile_count) {
    ntl_team_profile *profile =
        &u->usrs.ntl_team_profiles[u->usrs.ntl_active_team_profile];
    strncpy(S.profile_name, profile->name, sizeof S.profile_name - 1);
    S.profile_name[sizeof S.profile_name - 1] = 0;
  }
  S.players_open = true;
}
void ntl_team_update(tenv *env) {
  if (!S.ready || !env || !env->usr) return;
  S.env = env;

  user_settings *us = &env->usr->usrs;
  game_data *g = &env->usr->gdata;

  ntl_sync_active_credentials(us);
  ntl_ensure_client_id(us);

  /* Publish nickname/server transitions immediately. The stable eight-character
     client prefix lets NTL replace this player's old name instead of creating a
     second entry. */
  bool playing = g->conn == CONNECTED && g->curr_screen == PLAYING;
  const char *presence_server = playing ? us->server_address : "_GAME_MENU_";
  if (strcmp(S.last_nickname, us->nickname) != 0 ||
      strcmp(S.last_presence_server, presence_server) != 0 ||
      S.last_presence_playing != playing) {
    strncpy(S.last_nickname, us->nickname, sizeof S.last_nickname - 1);
    S.last_nickname[sizeof S.last_nickname - 1] = 0;
    strncpy(S.last_presence_server, presence_server,
            sizeof S.last_presence_server - 1);
    S.last_presence_server[sizeof S.last_presence_server - 1] = 0;
    S.last_presence_playing = playing;
    S.next_poll = 0.0;
  }

  mg_mgr_poll(&S.mgr, 0);
  double now = mg_millis() / 1000.0;

  snake *me = playing ? local_snake(g) : NULL;
  if (S.sos_until > 0.0 &&
      (now >= S.sos_until || !me || me->dead)) {
    S.sos_until = 0.0;
    vlither_chat_set_sos_until(0);
    S.next_poll = 0.0;
  }

  if (S.request_active &&
      now - S.request_started > NTL_REQUEST_TIMEOUT_SECONDS) {
    struct mg_connection *timed_out = S.request_conn;
    S.request_active = false;
    S.request_conn = NULL;
    if (timed_out) timed_out->is_closing = true;
    ntl_schedule_retry(now);
  }

  if (!S.request_active && now >= S.next_poll) {
    ntl_poll_request(env);
    if (S.request_active && S.next_poll < now + NTL_POLL_SECONDS)
      S.next_poll = now + NTL_POLL_SECONDS;
  }
}

void ntl_team_trigger_sos(void) {
  if (!S.env || !S.env->usr) return;
  game_data *g = &S.env->usr->gdata;
  user_settings *us = &S.env->usr->usrs;
  snake *me = (g->conn == CONNECTED && g->curr_screen == PLAYING)
                  ? local_snake(g) : NULL;
  if (!me || me->dead) return;
  S.sos_until = mg_millis() / 1000.0 + 240.0;
  vlither_chat_set_sos_until((long long)time(NULL) * 1000LL + 240000LL);
  if (vlither_chat_joined()) vlither_chat_send_text("Help me!");
  S.sos_message_pending = us->ntl_enabled &&
                          strlen(us->ntl_auth_key) >= 16 &&
                          strlen(us->ntl_team_id) >= 16;
  S.next_poll = 0.0;
}

bool ntl_team_local_sos_active(void) {
  return S.sos_until > mg_millis() / 1000.0;
}

static void normalize_server(char *out, size_t cap, const char *in) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (!in) return;

  while (isspace((unsigned char)*in)) in++;
  if (!strncmp(in, "ws://", 5)) in += 5;
  else if (!strncmp(in, "wss://", 6)) in += 6;
  else if (!strncmp(in, "http://", 7)) in += 7;
  else if (!strncmp(in, "https://", 8)) in += 8;

  size_t n = 0;
  while (*in && n + 1 < cap) {
    unsigned char c = (unsigned char)*in++;
    if (c == '/' || c == '?' || c == '#' || isspace(c)) break;
    out[n++] = (char)tolower(c);
  }
  while (n > 0 && (out[n - 1] == '/' || isspace((unsigned char)out[n - 1]))) n--;
  out[n] = 0;
}

static bool same_server(const char *a, const char *b) {
  char na[96], nb[96];
  normalize_server(na, sizeof na, a);
  normalize_server(nb, sizeof nb, b);
  return na[0] && nb[0] && !strcmp(na, nb);
}

static const char *ntl_clean_name(const char *name) {
  if (!name) return "Player";
  if (strlen(name) > 8) {
    bool prefix_is_hex = true;
    for (int i = 0; i < 8; ++i) {
      if (!isxdigit((unsigned char)name[i])) {
        prefix_is_hex = false;
        break;
      }
    }
    if (prefix_is_hex) return name + 8;
  }
  return name[0] ? name : "Player";
}

static bool ntl_name_equal(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
      return false;
  }
  return !*a && !*b;
}

int ntl_team_leaderboard_status(const char *nickname, const char *server) {
  if (!nickname || !nickname[0] || !server || !server[0]) return 0;
  if (vlither_chat_is_nickname_sos(nickname, server)) return 3;
  bool ntl_fresh = ntl_feed_is_fresh();
  for (int i = 0; ntl_fresh && i < S.count; ++i) {
    if (same_server(S.members[i].srv, server) &&
        ntl_name_equal(ntl_clean_name(S.members[i].nick), nickname) &&
        S.members[i].is_sos)
      return 3;
  }
  if (vlither_chat_is_nickname_player(nickname, server)) return 2;
  for (int i = 0; ntl_fresh && i < S.count; ++i) {
    if (same_server(S.members[i].srv, server) &&
        ntl_name_equal(ntl_clean_name(S.members[i].nick), nickname))
      return 1;
  }
  return 0;
}

static bool ntl_member_same_client(const ntl_member *a,
                                   const ntl_member *b) {
  if (!a || !b || strlen(a->nick) < 8 || strlen(b->nick) < 8) return false;
  for (int i = 0; i < 8; ++i) {
    unsigned char ca = (unsigned char)a->nick[i];
    unsigned char cb = (unsigned char)b->nick[i];
    if (!isxdigit(ca) || !isxdigit(cb) || tolower(ca) != tolower(cb))
      return false;
  }
  return true;
}

static bool ntl_member_is_local(const ntl_member *m,
                                const user_settings *us,
                                const snake *local) {
  if (!m || !us) return false;

  /* The eight-character client prefix survives nickname changes, respawns,
     server changes and secondary-ID updates. It is the safest way to reject
     our own echoed NTL presence record. */
  if (strlen(m->nick) >= 8 && strlen(us->ntl_client_id) == 8) {
    bool same_client = true;
    for (int i = 0; i < 8; ++i) {
      if (tolower((unsigned char)m->nick[i]) !=
          tolower((unsigned char)us->ntl_client_id[i])) {
        same_client = false;
        break;
      }
    }
    if (same_client) return true;
  }

  /* NTL's `sid` field is snake.ntlid (the secondary NTL ID), not the normal
     Slither snake ID. The old comparison used local->id and intermittently
     left a second, stale marker for the local player. */
  return local && m->sid >= 0 && m->sid == (int)local->ntl_id;
}

static bool ntl_member_has_vlither_presence(const ntl_member *m,
                                             const user_settings *us) {
  if (!m || !us || strlen(m->nick) < 8) return false;
  int count = vlither_chat_player_count();
  for (int i = 0; i < count; ++i) {
    const char *client_id = vlither_chat_player_client_id(i);
    if (!client_id || strlen(client_id) != 8 ||
        !same_server(vlither_chat_player_server(i), us->server_address))
      continue;
    bool same = true;
    for (int j = 0; j < 8; ++j) {
      if (tolower((unsigned char)m->nick[j]) !=
          tolower((unsigned char)client_id[j])) {
        same = false;
        break;
      }
    }
    if (same) return true;
  }
  return false;
}

static snake *ntl_visible_snake_for_member(game_data *g,
                                            const ntl_member *m,
                                            const snake *local) {
  if (!g || !m || m->sid < 0) return NULL;
  int count = tdarray_length(g->data.snakes);
  for (int i = 0; i < count; ++i) {
    snake *candidate = &g->data.snakes[i];
    if (candidate == local || candidate->dead || !candidate->iiv) continue;
    if ((int)candidate->ntl_id == m->sid) return candidate;
  }
  return NULL;
}

static void ntl_draw_marker(ImDrawList *dl, ImVec2 p, float radius,
                            int shape, ImU32 fill) {
  int r = (int)(fill & 0xffu);
  int g = (int)((fill >> 8) & 0xffu);
  int b = (int)((fill >> 16) & 0xffu);
  ImU32 border = r * 21 + g * 72 + b * 7 < 9000
                     ? IM_COL32(255, 255, 255, 225)
                     : IM_COL32(0, 0, 0, 210);
  float border_radius = radius + 1.4f;
  if (shape == 1) {
    ImVec2 outer[4] = {{p.x, p.y - border_radius},
                       {p.x + border_radius, p.y},
                       {p.x, p.y + border_radius},
                       {p.x - border_radius, p.y}};
    ImVec2 inner[4] = {{p.x, p.y - radius}, {p.x + radius, p.y},
                       {p.x, p.y + radius}, {p.x - radius, p.y}};
    ImDrawList_AddConvexPolyFilled(dl, outer, 4, border);
    ImDrawList_AddConvexPolyFilled(dl, inner, 4, fill);
  } else if (shape == 2) {
    ImVec2 o1 = {p.x, p.y - border_radius};
    ImVec2 o2 = {p.x + border_radius * 0.92f, p.y + border_radius * 0.80f};
    ImVec2 o3 = {p.x - border_radius * 0.92f, p.y + border_radius * 0.80f};
    ImVec2 i1 = {p.x, p.y - radius};
    ImVec2 i2 = {p.x + radius * 0.92f, p.y + radius * 0.80f};
    ImVec2 i3 = {p.x - radius * 0.92f, p.y + radius * 0.80f};
    ImDrawList_AddTriangleFilled(dl, o1, o2, o3, border);
    ImDrawList_AddTriangleFilled(dl, i1, i2, i3, fill);
  } else {
    ImDrawList_AddCircleFilled(dl, p, border_radius, border, 16);
    ImDrawList_AddCircleFilled(dl, p, radius, fill, 16);
  }
}

static void ntl_load_profile(user_settings *us, int index) {
  if (!us || index < 0 || index >= us->ntl_team_profile_count) return;
  ntl_team_profile *profile = &us->ntl_team_profiles[index];
  strncpy(us->ntl_team_id, profile->team_id, sizeof us->ntl_team_id - 1);
  us->ntl_team_id[sizeof us->ntl_team_id - 1] = 0;
  strncpy(us->ntl_auth_key, profile->auth_key, sizeof us->ntl_auth_key - 1);
  us->ntl_auth_key[sizeof us->ntl_auth_key - 1] = 0;
  strncpy(S.profile_name, profile->name, sizeof S.profile_name - 1);
  S.profile_name[sizeof S.profile_name - 1] = 0;
  us->ntl_active_team_profile = index;
  ntl_sync_active_credentials(us);
  S.next_poll = 0.0;
}

static int ntl_find_profile(const user_settings *us, const char *name) {
  if (!us || !name || !name[0]) return -1;
  for (int i = 0; i < us->ntl_team_profile_count; ++i)
    if (!strcmp(us->ntl_team_profiles[i].name, name)) return i;
  return -1;
}

static bool ntl_normalize_profile_name(char *name) {
  if (!name) return false;
  char *start = name;
  while (*start && isspace((unsigned char)*start)) start++;
  if (start != name) memmove(name, start, strlen(start) + 1);
  size_t len = strlen(name);
  while (len > 0 && isspace((unsigned char)name[len - 1])) name[--len] = 0;
  return len > 0;
}

static bool ntl_feed_is_fresh(void) {
  double now = mg_millis() / 1000.0;
  return S.last_success > 0.0 &&
         now - S.last_success <= NTL_CONNECTED_GRACE_SECONDS;
}

void ntl_team_draw_minimap(tenv *env, float x, float y, float size) {
  if (!env || !env->usr || size <= 0.0f) return;
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;
  if (g->conn != CONNECTED || g->data.grd <= 0.0f ||
      !isfinite(g->data.flux_grd) || g->data.flux_grd <= 1.0f)
    return;

  ImDrawList *dl = igGetWindowDrawList();
  if (!dl) return;
  float radius = size * 0.5f;
  /* Keep CPU markers in the exact coordinate space used by mm.slang. The
     minimap shader normalizes world positions by the current (possibly
     shrinking) border radius and then applies its 0.90 shadow scale. Using
     the fixed world-center value as the divisor makes a snake at a shrinking
     border appear incorrectly near the middle of the minimap. */
  float map_radius = radius * 0.90f;
  float world_radius = g->data.flux_grd;
  ImVec2 center = {x + radius, y + radius};

  /* Draw the local snake with the same dynamic-border transform used by the
     location percentage HUD and minimap shader. */
  snake *local = local_snake(g);
  if (local) {
    float rx = (local->xx - g->data.grd) / world_radius;
    float ry = (local->yy - g->data.grd) / world_radius;
    float dist = sqrtf(rx * rx + ry * ry);
    if (dist > 1.0f) { rx /= dist; ry /= dist; }
    ImVec2 p = {center.x + rx * map_radius, center.y + ry * map_radius};
    const char *local_color_name = us->minimap_display_name[0]
                                       ? us->minimap_display_name
                                       : us->nickname;
    ImU32 col = ntl_unique_u32(local_color_name, 0x564c4954u);
    ntl_draw_marker(dl, p, us->own_marker_size, us->own_marker_shape, col);
    if (us->minimap_show_own_name && local_color_name[0]) {
      ImVec2 ts;
      igCalcTextSize(&ts, local_color_name, NULL, false, -1.0f);
      ImDrawList_AddText_Vec2(dl,
          (ImVec2){p.x - ts.x * 0.5f, p.y + us->own_marker_size + 2.0f},
          col, local_color_name, NULL);
    }
  }

  /* Vlither Android presence is independent from NTL. Every Vlither client
     publishes its authoritative world coordinates to the Vlither backend once
     per second, so same-server players remain visible on the minimap even when
     they are outside this client's rendered snake range. */
  if (us->vlither_show_minimap_players && vlither_chat_connected()) {
    int count = vlither_chat_player_count();
    for (int i = 0; i < count; ++i) {
      const char *server = vlither_chat_player_server(i);
      const char *client_id = vlither_chat_player_client_id(i);
      int snake_id = vlither_chat_player_snake_id(i);
      if (!same_server(server, us->server_address) || snake_id < 0) continue;
      if ((local && snake_id == local->id) ||
          (client_id && us->ntl_client_id[0] &&
           !strcmp(client_id, us->ntl_client_id)))
        continue;

      float marker_x = vlither_chat_player_x(i);
      float marker_y = vlither_chat_player_y(i);
      snake *visible = get_snake(g, snake_id);
      if (visible && visible != local && !visible->dead && visible->iiv) {
        marker_x = visible->xx;
        marker_y = visible->yy;
      }
      if (!isfinite(marker_x) || !isfinite(marker_y) ||
          (marker_x == 0.0f && marker_y == 0.0f))
        continue;

      float rx = (marker_x - g->data.grd) / world_radius;
      float ry = (marker_y - g->data.grd) / world_radius;
      float dist = sqrtf(rx * rx + ry * ry);
      if (dist > 1.0f) { rx /= dist; ry /= dist; }
      ImVec2 p = {center.x + rx * map_radius, center.y + ry * map_radius};
      const char *color_name = vlither_chat_player_nick(i);
      ImVec4 profile_col = ntl_vlither_profile_color(
          vlither_chat_player_color(i), color_name);
      ImU32 marker_col = igColorConvertFloat4ToU32(profile_col);
      ntl_draw_marker(dl, p, us->ntl_marker_size, us->ntl_marker_shape,
                      marker_col);

      if (us->ntl_marker_labels) {
        const char *name = vlither_chat_player_map_name(i);
        if (!name || !name[0]) name = vlither_chat_player_nick(i);
        if (!name || !name[0]) name = "Vlither";
        char profile_label[96];
        ntl_vlither_profile_label(profile_label, sizeof profile_label,
                                  vlither_chat_player_emoji(i), name);
        name = profile_label;
        igPushFont(u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR],
                   u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR]->LegacySize);
        ImVec2 text_size;
        igCalcTextSize(&text_size, name, NULL, false, -1.0f);
        ImVec2 text_pos = {p.x - text_size.x * 0.5f,
                           p.y + us->ntl_marker_size + 2.0f};
        for (int ox = -1; ox <= 1; ++ox)
          for (int oy = -1; oy <= 1; ++oy)
            if (ox || oy)
              ImDrawList_AddText_Vec2(dl,
                  (ImVec2){text_pos.x + ox, text_pos.y + oy},
                  IM_COL32(0, 0, 0, 235), name, NULL);
        ImDrawList_AddText_Vec2(dl, text_pos, marker_col, name, NULL);
        igPopFont();
      }
    }
  }

  if (!us->ntl_enabled || !us->ntl_show_teammates || !ntl_feed_is_fresh())
    return;

  /* NTL teammates use the same visual marker style. If the same Vlither
     Android client is present in both feeds, prefer the Vlither copy to avoid
     drawing duplicate dots/names. */
  for (int i = 0; i < S.count; ++i) {
    ntl_member *m = &S.members[i];
    if (!same_server(m->srv, us->server_address) ||
        ntl_member_is_local(m, us, local) ||
        ntl_member_has_vlither_presence(m, us))
      continue;

    float marker_x = m->x;
    float marker_y = m->y;

    /* When the teammate's snake is currently known by this game client, use
       its live Slither coordinates. The NTL team endpoint updates roughly
       once per second, so the network copy naturally trails fast movement. */
    snake *visible = ntl_visible_snake_for_member(g, m, local);
    if (visible) {
      marker_x = visible->xx;
      marker_y = visible->yy;
    }

    /* A reconnect or respawn can briefly leave two endpoint records carrying
       the same stable client prefix. Draw only the best copy: a record that
       maps to a live snake wins; otherwise the later endpoint record wins. */
    int quality = visible ? 2 :
        (isfinite(marker_x) && isfinite(marker_y) &&
         !(marker_x == 0.0f && marker_y == 0.0f) ? 1 : 0);
    bool superseded = false;
    for (int j = i + 1; j < S.count; ++j) {
      ntl_member *other = &S.members[j];
      if (!same_server(other->srv, us->server_address) ||
          !ntl_member_same_client(m, other))
        continue;
      snake *other_visible =
          ntl_visible_snake_for_member(g, other, local);
      int other_quality = other_visible ? 2 :
          (isfinite(other->x) && isfinite(other->y) &&
           !(other->x == 0.0f && other->y == 0.0f) ? 1 : 0);
      if (other_quality >= quality) {
        superseded = true;
        break;
      }
    }
    if (superseded) continue;

    if (!isfinite(marker_x) || !isfinite(marker_y) ||
        (marker_x == 0.0f && marker_y == 0.0f))
      continue;

    float rx = (marker_x - g->data.grd) / world_radius;
    float ry = (marker_y - g->data.grd) / world_radius;
    float dist = sqrtf(rx * rx + ry * ry);
    if (dist > 1.0f) { rx /= dist; ry /= dist; }
    ImVec2 p = {center.x + rx * map_radius, center.y + ry * map_radius};
    const char *clean_name = ntl_clean_name(m->nick);
    ImU32 marker_col = ntl_unique_u32(clean_name, 0x4e544c31u);
    ntl_draw_marker(dl, p, us->ntl_marker_size, us->ntl_marker_shape,
                    marker_col);

    if (us->ntl_marker_labels) {
      const char *name = clean_name;
      igPushFont(u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR],
                 u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR]->LegacySize);
      ImVec2 text_size;
      igCalcTextSize(&text_size, name, NULL, false, -1.0f);
      ImVec2 text_pos = {p.x - text_size.x * 0.5f,
                         p.y + us->ntl_marker_size + 2.0f};
      for (int ox = -1; ox <= 1; ++ox)
        for (int oy = -1; oy <= 1; ++oy)
          if (ox || oy)
            ImDrawList_AddText_Vec2(dl,
                (ImVec2){text_pos.x + ox, text_pos.y + oy},
                IM_COL32(0, 0, 0, 235), name, NULL);
      ImDrawList_AddText_Vec2(dl, text_pos, marker_col, name, NULL);
      igPopFont();
    }
  }
}

void ntl_team_consume_ui_touch(tenv *env) {
  (void)env;
  /* Android input ownership now prevents NTL-window touches from entering
     gameplay at ACTION_DOWN. Preserve any separate trackpad finger. */
}

static void draw_chat_network_switch(void) {
  const float button_w = 110.0f;
  bool ntl_active = !S.vlither_chat_active;
  bool vlither_active = S.vlither_chat_active;
  if (ntl_active)
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.16f, 0.52f, 0.72f, 0.92f});
  else
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.20f, 0.22f, 0.28f, 0.92f});
  if (igButton("NTL##chat_network", (ImVec2){button_w, 0})) {
    S.vlither_chat_active = false;
  }
  igPopStyleColor(1);
  igSameLine(0, 6);
  if (vlither_active)
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.35f, 0.35f, 0.82f, 0.92f});
  else
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.20f, 0.22f, 0.28f, 0.92f});
  if (igButton("Vlither##chat_network", (ImVec2){button_w, 0})) {
    S.vlither_chat_active = true;
  }
  igPopStyleColor(1);
  igSameLine(0, 8);
  if (S.vlither_chat_active) {
    bool joined = vlither_chat_joined();
    igTextColored(!joined
                      ? (ImVec4){0.65f, 0.65f, 0.68f, 0.9f}
                      : vlither_chat_connected()
                      ? (ImVec4){0.35f, 1.0f, 0.50f, 0.9f}
                      : (ImVec4){1.0f, 0.72f, 0.25f, 0.9f},
                  !joined ? "Left"
                          : vlither_chat_connected() ? "Connected"
                                                     : "Reconnecting");
  }
}

static void chat_submit_current(const user_settings *us) {
  if (S.vlither_chat_active) {
    ntl_normalize_chat_text(S.input, sizeof S.input);
    if (!S.input[0]) return;
    if (vlither_chat_send_text(S.input)) S.input[0] = 0;
    return;
  }
  ntl_queue_message(us);
}

static void ntl_draw_wrapped_color(ImVec4 color, const char *text) {
  igPushStyleColor_Vec4(ImGuiCol_Text, color);
  igTextWrapped("%s", text);
  igPopStyleColor(1);
}

static void ntl_player_stats_text(char *out, size_t out_size, int fps,
                                  int ping) {
  out[0] = 0;
  if (fps >= 0 && ping >= 0)
    snprintf(out, out_size, "%d FPS | %d ms", fps, ping);
  else if (fps >= 0)
    snprintf(out, out_size, "%d FPS", fps);
  else if (ping >= 0)
    snprintf(out, out_size, "%d ms", ping);
}

static void ntl_draw_vlither_player_row(user_settings *us, int i,
                                        bool compact) {
  const char *player_name = vlither_chat_player_nick(i);
  char player_label[96];
  ntl_vlither_profile_label(player_label, sizeof player_label,
                            vlither_chat_player_emoji(i), player_name);
  ImVec4 name_color = ntl_vlither_text_color(
      vlither_chat_player_color(i), player_name);
  int fps = vlither_chat_player_fps(i);
  int ping = vlither_chat_player_ping(i);

  if (!compact) {
    igTextColored(name_color, "● %s", player_label);
    igSameLine(0, 4);
    igTextColored((ImVec4){0.2f, 0.8f, 0.8f, 0.8f}, "%s",
                  vlither_chat_player_server(i));
    igSameLine(0, 4);
    igTextColored((ImVec4){0.2f, 0.8f, 0.8f, 0.8f}, "Vlither v%s",
                  vlither_chat_player_version(i));
    if (us->vlither_show_player_stats && (fps >= 0 || ping >= 0)) {
      char stats[64];
      ntl_player_stats_text(stats, sizeof stats, fps, ping);
      igSameLine(0, 4);
      igTextColored((ImVec4){0.78f, 0.78f, 0.82f, 0.9f}, "%s", stats);
    }
    return;
  }

  char first_line[112];
  snprintf(first_line, sizeof first_line, "● %s", player_label);
  ntl_draw_wrapped_color(name_color, first_line);

  char stats[64];
  ntl_player_stats_text(stats, sizeof stats, fps, ping);
  char metadata[256];
  snprintf(metadata, sizeof metadata, "%s | Vlither v%s%s%s",
           vlither_chat_player_server(i), vlither_chat_player_version(i),
           us->vlither_show_player_stats && stats[0] ? " | " : "",
           us->vlither_show_player_stats ? stats : "");
  ntl_draw_wrapped_color((ImVec4){0.62f, 0.76f, 0.80f, 0.9f}, metadata);
}

static void ntl_draw_team_player_row(user_settings *us, ntl_member *m,
                                     bool compact) {
  const char *clean_name = ntl_clean_name(m->nick);
  ImVec4 name_color = ntl_unique_color(clean_name, 0x4e544c31u);
  const char *owner = m->owner[0] ? m->owner : "unknown";
  const char *server = m->srv[0] ? m->srv : "_GAME_MENU_";
  char version[40];
  if (m->has_vlither_telemetry && m->vlither_ver[0])
    snprintf(version, sizeof version, "Vlither v%s", m->vlither_ver);
  else
    snprintf(version, sizeof version, "NTL v%s",
             m->ver[0] ? m->ver : "?");

  if (!compact) {
    igTextColored((ImVec4){0.85f, 0.2f, 0.2f, 1.0f}, "%s", owner);
    igSameLine(0, 4);
    igTextColored(name_color, "● %s", clean_name);
    igSameLine(0, 4);
    igTextColored((ImVec4){0.2f, 0.8f, 0.8f, 0.8f}, "%s", server);
    igSameLine(0, 4);
    igTextColored((ImVec4){0.2f, 0.8f, 0.8f, 0.8f}, "%s", version);
    if (us->ntl_show_player_stats && m->has_telemetry &&
        (m->fps >= 0 || m->ping >= 0)) {
      char stats[64];
      ntl_player_stats_text(stats, sizeof stats, m->fps, m->ping);
      igSameLine(0, 4);
      igTextColored((ImVec4){0.78f, 0.78f, 0.82f, 0.9f}, "%s", stats);
    }
    return;
  }

  char first_line[112];
  snprintf(first_line, sizeof first_line, "● %s", clean_name);
  ntl_draw_wrapped_color(name_color, first_line);

  char stats[64];
  ntl_player_stats_text(stats, sizeof stats, m->fps, m->ping);
  char metadata[320];
  snprintf(metadata, sizeof metadata, "%s | %s | %s%s%s", owner, server,
           version,
           us->ntl_show_player_stats && m->has_telemetry && stats[0]
               ? " | "
               : "",
           us->ntl_show_player_stats && m->has_telemetry ? stats : "");
  ntl_draw_wrapped_color((ImVec4){0.62f, 0.76f, 0.80f, 0.9f}, metadata);
}

static void ntl_draw_friends_zoom_control(user_settings *us,
                                          const char *slider_id) {
  igText("Friends / players zoom");
  igSetNextItemWidth(-1.0f);
  igSliderFloat(slider_id, &us->friends_panel_zoom, 0.75f, 1.50f,
                "%.2fx", ImGuiSliderFlags_AlwaysClamp);
  if (igIsItemDeactivatedAfterEdit()) save_user_settings(us);
  if (igIsItemHovered(0))
    igSetTooltip("Changes text size in Online Players. Narrow panels switch "
                 "to a wrapped two-line layout automatically.");
}

void ntl_team_draw(tenv *env) {
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;
  if (g->conn != CONNECTED) return;

  /* Match the lightweight S.zip NTL HUD while playing. Both NTL Chat and the
     online-player list are hidden on Settings; their Android size/position is
     edited from Keyboard Editor instead. */
  bool settings_open = g->curr_screen == SETTINGS;
  if (us->ntl_stealth_mode && !settings_open) return;
  ImGuiViewport *vp = igGetMainViewport();
  ImGuiStyle *style = igGetStyle();

  igPushFont(u->imgui_data.regular_font[FONT_SIZE_SMALL],
             u->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 8.0f);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){8.0f, 8.0f});
  igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                        (ImVec4){0.08f, 0.08f, 0.10f, 0.65f});

  /* Keep NTL Chat completely hidden while the Settings screen is open.
     Its Android HUD position/size is edited from the Keyboard Editor instead,
     so Settings no longer exposes or overlays the chat window. */
  bool *active_chat_open = S.vlither_chat_active
                               ? &S.vlither_chat_open
                               : &S.ntl_chat_open;
  if (*active_chat_open && !settings_open) {
    ImGuiWindowFlags chat_flags = ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoSavedSettings;
    if (!settings_open) {
      chat_flags |= ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoBackground;
    }

#ifdef ANDROID
    float chat_max_w = fmaxf(220.0f, vp->WorkSize.x * 0.92f);
    float chat_max_h = fmaxf(150.0f, vp->WorkSize.y * 0.88f);
    float chat_min_w = fminf(260.0f, chat_max_w);
    float chat_min_h = fminf(150.0f, chat_max_h);
    float chat_w = ntl_clampf(us->ntl_chat_rel_w * vp->WorkSize.x,
                              chat_min_w, chat_max_w);
    float chat_h = ntl_clampf(us->ntl_chat_rel_h * vp->WorkSize.y,
                              chat_min_h, chat_max_h);
    float chat_x = vp->WorkPos.x + us->ntl_chat_rel_x * vp->WorkSize.x;
    float chat_y = vp->WorkPos.y + us->ntl_chat_rel_y * vp->WorkSize.y;
    chat_x = ntl_clampf(chat_x, vp->WorkPos.x,
                        vp->WorkPos.x + vp->WorkSize.x - chat_w);
    chat_y = ntl_clampf(chat_y, vp->WorkPos.y,
                        vp->WorkPos.y + vp->WorkSize.y - chat_h);
    igSetNextWindowPos((ImVec2){chat_x, chat_y}, ImGuiCond_Appearing,
                       (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){chat_w, chat_h}, ImGuiCond_Appearing);
    if (settings_open)
      igSetNextWindowSizeConstraints((ImVec2){chat_min_w, chat_min_h},
                                     (ImVec2){chat_max_w, chat_max_h},
                                     NULL, NULL);
#else
    igSetNextWindowPos((ImVec2){20.0f, vp->WorkPos.y + vp->WorkSize.y - 220.0f},
                       ImGuiCond_FirstUseEver, (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){360.0f, 180.0f}, ImGuiCond_FirstUseEver);
    if (settings_open)
      igSetNextWindowSizeConstraints((ImVec2){200.0f, 100.0f},
                                     (ImVec2){800.0f, 600.0f}, NULL, NULL);
#endif

    if (igBegin("Chat##ntl_hud", active_chat_open, chat_flags)) {
      ImVec2 win_sz;
      igGetWindowSize(&win_sz);

#ifdef ANDROID
      ImVec2 chat_pos_now, chat_size_now;
      igGetWindowPos(&chat_pos_now);
      igGetWindowSize(&chat_size_now);
      if (settings_open) {
        android_ui_capture_rect(chat_pos_now.x, chat_pos_now.y,
                                chat_pos_now.x + chat_size_now.x,
                                chat_pos_now.y + chat_size_now.y);
      } else {
        /* The message history is display-only in gameplay. Capture only the
           input strip so the transparent S-style HUD does not steal a large
           trackpad area from movement. */
        float input_capture_h = igGetFrameHeight() + 16.0f;
        android_ui_capture_rect(
            chat_pos_now.x,
            fmaxf(chat_pos_now.y,
                  chat_pos_now.y + chat_size_now.y - input_capture_h),
            chat_pos_now.x + chat_size_now.x,
            chat_pos_now.y + chat_size_now.y);
        /* The network selector at the top is interactive as well. Capture only
           that strip so the transparent center of the chat remains usable as
           the movement trackpad. */
        float switch_capture_h = igGetFrameHeight() + 14.0f;
        android_ui_capture_rect(chat_pos_now.x, chat_pos_now.y,
                                chat_pos_now.x + chat_size_now.x,
                                chat_pos_now.y + switch_capture_h);
      }
      if (settings_open && vp->WorkSize.x > 0.0f && vp->WorkSize.y > 0.0f) {
        us->ntl_chat_rel_x = (chat_pos_now.x - vp->WorkPos.x) / vp->WorkSize.x;
        us->ntl_chat_rel_y = (chat_pos_now.y - vp->WorkPos.y) / vp->WorkSize.y;
        us->ntl_chat_rel_w = chat_size_now.x / vp->WorkSize.x;
        us->ntl_chat_rel_h = chat_size_now.y / vp->WorkSize.y;
        if (S.chat_w > 0.0f &&
            (fabsf(S.chat_x - chat_pos_now.x) > 0.5f ||
             fabsf(S.chat_y - chat_pos_now.y) > 0.5f ||
             fabsf(S.chat_w - chat_size_now.x) > 0.5f ||
             fabsf(S.chat_h - chat_size_now.y) > 0.5f))
          S.layout_dirty = true;
        S.chat_x = chat_pos_now.x;
        S.chat_y = chat_pos_now.y;
        S.chat_w = chat_size_now.x;
        S.chat_h = chat_size_now.y;
      }
#endif

      draw_chat_network_switch();
      float switch_h = igGetFrameHeight() + style->ItemSpacing.y;
      float input_h = igGetFrameHeight() + style->ItemSpacing.y;
      float message_h = fmaxf(40.0f, win_sz.y - input_h - switch_h - 20.0f);
      igBeginChild_Str("##messages_list_box",
                       (ImVec2){fmaxf(40.0f, win_sz.x - 16.0f), message_h},
                       false, ImGuiWindowFlags_None);
      igPushTextWrapPos(win_sz.x - 24.0f);
      if (S.vlither_chat_active) {
        int vcount = vlither_chat_history_count();
        if (vcount == 0) {
          igTextColored((ImVec4){0.60f, 0.60f, 0.60f, 0.75f},
                        !vlither_chat_joined()
                            ? "You left Vlither Global Chat."
                            : vlither_chat_connected()
                            ? "No Vlither messages yet."
                            : "Vlither chat is reconnecting...");
        }
        for (int n = 0; n < vcount; ++n) {
          const char *sender = vlither_chat_history_nick(n);
          const char *text = vlither_chat_history_text(n);
          if (us->ntl_chat_timestamps) {
            char clock[6];
            ntl_history_clock(
                (time_t)(vlither_chat_history_time_ms(n) / 1000LL), clock);
            igTextColored((ImVec4){0.62f, 0.66f, 0.72f, 0.86f}, "%s ", clock);
            igSameLine(0, 0);
          }
          char sender_label[96];
          ntl_vlither_profile_label(sender_label, sizeof sender_label,
                                    vlither_chat_history_emoji(n), sender);
          igTextColored(ntl_vlither_text_color(
                            vlither_chat_history_color(n), sender),
                        "[%s]: ", sender_label);
          igSameLine(0, 0);
          igTextColored((ImVec4){0.92f, 0.92f, 0.96f, 1.0f}, "%s", text);
        }
        if (vcount != S.last_vlither_history_count) {
          igSetScrollHereY(1.0f);
          S.last_vlither_history_count = vcount;
        }
      } else {
        if (!us->ntl_enabled) {
          igTextColored((ImVec4){1.0f, 0.70f, 0.25f, 0.9f},
                        "NTL chat is disabled. Open Chat from the homepage to configure it, or switch to Vlither chat.");
        } else if (S.history_count == 0) {
          igTextColored((ImVec4){0.60f, 0.60f, 0.60f, 0.75f},
                        "No team messages yet.");
        }
        for (int n = 0; n < S.history_count; ++n) {
          int i = (S.history_start + n) % NTL_CHAT_HISTORY_MAX;
          const char *sender = S.history[i].nick[0] ? S.history[i].nick : "Player";
          bool system_msg = !strcmp(sender, "System");
          ImVec4 sender_col = system_msg
                                  ? (ImVec4){0.90f, 0.70f, 0.20f, 1.0f}
                                  : ntl_unique_color(sender, 0x4e544c31u);
          ImVec4 text_col = system_msg
                                ? (ImVec4){0.90f, 0.85f, 0.60f, 1.0f}
                                : (ImVec4){0.90f, 0.90f, 0.92f, 1.0f};
          if (us->ntl_chat_timestamps) {
            char clock[6];
            ntl_history_clock(S.history[i].time, clock);
            igTextColored((ImVec4){0.62f, 0.66f, 0.72f, 0.86f}, "%s ", clock);
            igSameLine(0, 0);
          }
          igTextColored(sender_col, "[%s]: ", sender);
          igSameLine(0, 0);
          igTextColored(text_col, "%s", S.history[i].text);
        }
        if (S.scroll_chat_bottom) {
          igSetScrollHereY(1.0f);
          S.scroll_chat_bottom = false;
        }
      }
      igPopTextWrapPos();
      igEndChild();

      igSetCursorPosY(fmaxf(0.0f, win_sz.y - igGetFrameHeight() - 8.0f));
      igPushItemWidth(fmaxf(40.0f, win_sz.x - 16.0f));
      /* Glass-style message field */
      igPushStyleColor_Vec4(ImGuiCol_FrameBg,
                            (ImVec4){0.08f, 0.10f, 0.14f, 0.28f});
      igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered,
                            (ImVec4){0.12f, 0.16f, 0.22f, 0.40f});
      igPushStyleColor_Vec4(ImGuiCol_FrameBgActive,
                            (ImVec4){0.14f, 0.20f, 0.28f, 0.48f});
      igPushStyleColor_Vec4(ImGuiCol_Text,
                            (ImVec4){0.95f, 0.97f, 1.0f, 0.92f});
      igPushStyleColor_Vec4(ImGuiCol_TextDisabled,
                            (ImVec4){0.75f, 0.80f, 0.88f, 0.55f});
      igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 10.0f);
      igPushStyleVar_Float(ImGuiStyleVar_FrameBorderSize, 1.0f);
      igPushStyleColor_Vec4(ImGuiCol_Border,
                            (ImVec4){1.0f, 1.0f, 1.0f, 0.18f});
      igBeginDisabled(S.vlither_chat_active && !vlither_chat_joined());
      bool submitted = igInputTextWithHint(
          "##chat_box_input",
          S.vlither_chat_active ? "Message Vlither chat..." : "Message NTL team...",
          S.input, sizeof S.input, ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
      igEndDisabled();
      igPopStyleColor(1); /* Border */
      igPopStyleVar(2);
      igPopStyleColor(5);
      igPopItemWidth();
      if (submitted) chat_submit_current(us);
    }
    igEnd();
  }

  /* Keep the online-player list hidden on Settings as well. Its layout is
     controlled from Keyboard Editor, matching the NTL Chat treatment. */
  if (S.players_open && !settings_open) {
    ImGuiWindowFlags player_flags = ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings;
    player_flags |= ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoBackground;

#ifdef ANDROID
    /* Never let the stored layout become larger than the current phone's
       usable area (rotation, cut-outs and split-screen can all shrink it). */
    float players_max_w =
        fminf(vp->WorkSize.x, fmaxf(220.0f, vp->WorkSize.x * 0.82f));
    float players_max_h =
        fminf(vp->WorkSize.y, fmaxf(120.0f, vp->WorkSize.y * 0.88f));
    players_max_w = fmaxf(1.0f, players_max_w);
    players_max_h = fmaxf(1.0f, players_max_h);
    float players_min_w = fminf(250.0f, players_max_w);
    float players_min_h = fminf(120.0f, players_max_h);
    float pw = ntl_clampf(us->ntl_players_rel_w * vp->WorkSize.x,
                          players_min_w, players_max_w);
    float ph = ntl_clampf(us->ntl_players_rel_h * vp->WorkSize.y,
                          players_min_h, players_max_h);
    float px = vp->WorkPos.x + us->ntl_players_rel_x * vp->WorkSize.x;
    float py = vp->WorkPos.y + us->ntl_players_rel_y * vp->WorkSize.y;
    px = ntl_clampf(px, vp->WorkPos.x, vp->WorkPos.x + vp->WorkSize.x - pw);
    py = ntl_clampf(py, vp->WorkPos.y, vp->WorkPos.y + vp->WorkSize.y - ph);
    {
      /* The stored layout is a fraction of the screen, so the default
         top-right spot that sits beside the leaderboard in landscape lands
         right on top of it in portrait. Drop the panel below the leaderboard
         instead — only when it would actually overlap, and only in
         portrait, so landscape and saved custom layouts are untouched. */
      extern float g_leaderboard_left_x, g_leaderboard_bottom_y;
      bool portrait_now = vp->WorkSize.y > vp->WorkSize.x;
      if (portrait_now && g_leaderboard_bottom_y > 0.0f &&
          px + pw > g_leaderboard_left_x && py < g_leaderboard_bottom_y) {
        float below = g_leaderboard_bottom_y + 8.0f;
        float max_py = vp->WorkPos.y + vp->WorkSize.y - ph;
        py = below < max_py ? below : max_py;
      }
    }
    igSetNextWindowPos((ImVec2){px, py}, ImGuiCond_Appearing, (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){pw, ph}, ImGuiCond_Appearing);
#else
    igSetNextWindowPos((ImVec2){20.0f, vp->WorkPos.y + 150.0f},
                       ImGuiCond_FirstUseEver, (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){250.0f, 150.0f}, ImGuiCond_FirstUseEver);
#endif
    igSetNextWindowBgAlpha(0.6f);

    if (igBegin("Online Players##ntl_hud", &S.players_open, player_flags)) {
      igPushFont(u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR],
                 u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR]->LegacySize *
                     us->friends_panel_zoom);
      ImVec2 players_avail;
      igGetContentRegionAvail(&players_avail);
      bool compact_players =
          players_avail.x < 430.0f * us->friends_panel_zoom;
      if (S.vlither_chat_active) {
        ntl_draw_wrapped_color((ImVec4){1.0f, 1.0f, 1.0f, 0.8f},
                               "Vlither players (nick, srv, ver):");
        int count = vlither_chat_player_count();
        for (int i = 0; i < count; ++i)
          ntl_draw_vlither_player_row(us, i, compact_players);
        if (!count)
          igTextColored((ImVec4){0.6f, 0.6f, 0.6f, 1.0f},
                        "No Vlither players online");
      } else {
        ntl_draw_wrapped_color(
            (ImVec4){1.0f, 1.0f, 1.0f, 0.8f},
            "Online players (key owner, nick, srv, ver):");
        int visible = 0;
        for (int i = 0; i < S.count; ++i) {
          ntl_member *m = &S.members[i];
          if (!m->nick[0] || !strcmp(m->nick, "00000000")) continue;
          ++visible;
          ntl_draw_team_player_row(us, m, compact_players);
        }
        if (!visible)
          igTextColored((ImVec4){0.6f, 0.6f, 0.6f, 1.0f},
                        "No team players online");
      }
      igPopFont();
    }
    igEnd();
  }

#ifdef ANDROID
  if (S.layout_dirty && igIsMouseReleased_Nil(0)) {
    save_user_settings(us);
    S.layout_dirty = false;
  }
#endif

  igPopStyleColor(1);
  igPopStyleVar(2);
  igPopFont();
}

void ntl_team_destroy(tenv *env) {
  (void)env;
  if (S.ready) mg_mgr_free(&S.mgr);
  memset(&S, 0, sizeof S);
}

static void ntl_queue_message(const user_settings *us) {
  ntl_normalize_chat_text(S.input, sizeof S.input);
  if (!S.input[0]) return;
  if (vlither_tags_handle_command(S.input)) {
    S.input[0] = 0;
    return;
  }
  if (ntl_tags_handle_command(S.input)) {
    S.input[0] = 0;
    return;
  }
  if (server_bd_id_handle_command(S.env, S.input)) {
    S.input[0] = 0;
    return;
  }

  double now = mg_millis() / 1000.0;
  if (!strcmp(S.last_sent_text, S.input) && now - S.last_sent_time < 1.5) {
    S.input[0] = 0;
    return;
  }

  /* Do not insert a local chat echo here. The NTL response is the
     authoritative feed and will add this message once when the server echoes
     it for our persistent client ID. Adding it locally as well made every
     outgoing message appear twice. */
  (void)us;
  strncpy(S.pending_msg, S.input, sizeof S.pending_msg - 1);
  S.pending_msg[sizeof S.pending_msg - 1] = 0;
  strncpy(S.last_sent_text, S.input, sizeof S.last_sent_text - 1);
  S.last_sent_text[sizeof S.last_sent_text - 1] = 0;
  S.last_sent_time = now;
  S.input[0] = 0;
  S.next_poll = 0;
}

static void draw_chat_how_to_use_popup(void) {
  ImGuiViewport *vp = igGetMainViewport();
  float width = fminf(620.0f, vp->WorkSize.x - 24.0f);
  float height = fminf(360.0f, vp->WorkSize.y - 24.0f);
  igSetNextWindowSize((ImVec2){width, height}, ImGuiCond_Appearing);
  if (!igBeginPopupModal("How to use Chat", NULL,
                         ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoCollapse))
    return;

  float close_space = igGetFrameHeight() + igGetStyle()->ItemSpacing.y * 2.0f;
  igBeginChild_Str("##chat_help_body", (ImVec2){0, -close_space},
                   ImGuiChildFlags_None,
                   ImGuiWindowFlags_AlwaysVerticalScrollbar);
  igBulletText("Open Chat from the homepage, then choose NTL or Vlither.");
  igBulletText("NTL needs your Team ID and Auth Key. Save a team to switch credentials later.");
  igBulletText("Vlither connects automatically and shows online Vlither players.");
  igBulletText("Use the message box and Send button; only live messages are shown.");
  igEndChild();

  igSeparator();
  if (igButton("Close##chat_help", (ImVec2){-1, 0}))
    igCloseCurrentPopup();
  igEndPopup();
}

void ntl_team_panel(tenv *env) {
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;
  ImGuiStyle *style = igGetStyle();
  ImVec2 avail;
  igGetContentRegionAvail(&avail);

  igPushFont(u->imgui_data.regular_font[us->ui_font_size],
             u->imgui_data.regular_font[us->ui_font_size]->LegacySize);

  igText("Chat Network");
  igSameLine(0, 14);
  draw_chat_network_switch();
  igSameLine(0, 12);
  if (igButton("How to use", (ImVec2){0, 0}))
    igOpenPopup_Str("How to use Chat", 0);
  igSeparator();
  draw_chat_how_to_use_popup();
  igGetContentRegionAvail(&avail);

  if (S.vlither_chat_active) {
    bool chat_joined = vlither_chat_joined();
    igTextColored(!chat_joined
                      ? (ImVec4){0.65f, 0.65f, 0.68f, 1.0f}
                      : vlither_chat_connected()
                      ? (ImVec4){0.35f, 1.0f, 0.5f, 1.0f}
                      : (ImVec4){1.0f, 0.7f, 0.25f, 1.0f},
                  !chat_joined
                      ? "Vlither Chat left"
                      : vlither_chat_connected()
                      ? "Vlither Chat connected"
                      : "Vlither Chat reconnecting...");
    igSameLine(0, 12);
    if (igButton(chat_joined ? "Leave Global Chat" : "Join Global Chat",
                 (ImVec2){180.0f, igGetFrameHeight() * 1.20f})) {
      vlither_chat_set_joined(!chat_joined);
      chat_joined = !chat_joined;
    }
    igTextWrapped("Vlither Chat is the public chat for players currently connected through the Vlither Android backend. The player list below shows online Vlither clients.");
    if (!chat_joined)
      igTextDisabled("While left, your presence, minimap marker, player-list entry, and chat messages are hidden from Vlither users.");
    if (igCheckbox("Show Vlither in-game chat", &S.vlither_chat_open)) {
      us->vlither_chat_hud_visible = S.vlither_chat_open;
      save_user_settings(us);
    }
    igSameLine(0, 16);
    igCheckbox("Show Vlither players", &S.players_open);
    igCheckbox("Show same-server Vlither players on minimap",
               &us->vlither_show_minimap_players);
    igCheckbox("Show FPS & ping in player list##vlither",
               &us->vlither_show_player_stats);
    ntl_draw_friends_zoom_control(us, "##vlither_friends_zoom");
    igTextDisabled("Minimap players use the Team dots style from NTL Players & minimap.");
    /* Closed by default so short phone screens retain the full chat body.
       Every control inside uses the current content width and therefore
       stacks naturally instead of clipping on narrow devices. */
    if (igCollapsingHeader_TreeNodeFlags("My map colour & emoji",
                                         ImGuiTreeNodeFlags_None)) {
      bool profile_changed = false;
      if (igCheckbox("Use my chosen colour",
                     &us->vlither_profile_color_custom))
        profile_changed = true;
      ImVec2 profile_avail;
      igGetContentRegionAvail(&profile_avail);
      if (profile_avail.x >= 390.0f) igSameLine(0, 12);
      igSetNextItemWidth(fminf(180.0f, profile_avail.x));
      if (igColorEdit3("##vlither_profile_colour", us->vlither_profile_color,
                       ImGuiColorEditFlags_NoInputs)) {
        us->vlither_profile_color_custom = true;
        profile_changed = true;
      }
      int emoji_count = vlither_profile_emoji_count();
      if (us->vlither_profile_emoji < 0 ||
          us->vlither_profile_emoji >= emoji_count)
        us->vlither_profile_emoji = 0;
      const char *selected_emoji =
          vlither_profile_emoji_at(us->vlither_profile_emoji);
      char emoji_preview[32];
      snprintf(emoji_preview, sizeof emoji_preview, "%s",
               selected_emoji[0] ? selected_emoji : "None");
      igText("Emoji badge");
      igSetNextItemWidth(fminf(180.0f, profile_avail.x));
      if (igBeginCombo("##vlither_profile_emoji", emoji_preview,
                       ImGuiComboFlags_None)) {
        for (int i = 0; i < emoji_count; ++i) {
          const char *emoji = vlither_profile_emoji_at(i);
          char option[40];
          snprintf(option, sizeof option, "%s##vlither_emoji_%d",
                   emoji[0] ? emoji : "None", i);
          if (igSelectable_Bool(option, us->vlither_profile_emoji == i,
                                ImGuiSelectableFlags_None, (ImVec2){0, 0})) {
            us->vlither_profile_emoji = i;
            profile_changed = true;
          }
        }
        igEndCombo();
      }
      igTextWrapped("Other Vlither players see this in chat, the player list and on the map. It is included in Settings Backup.");
      if (profile_changed) {
        save_user_settings(us);
        vlither_chat_profile_changed();
      }
    }
    igSeparator();

    /* Keep navigation visible on short/wide Android displays.  The previous
       layout reserved space for a footer and then forced the chat body to a
       large minimum height, which could push both the Back control and the
       message input below the physical screen. */
    ImVec2 nav_avail_v;
    igGetContentRegionAvail(&nav_avail_v);
    float nav_w_v = (nav_avail_v.x - style->ItemSpacing.x) * 0.5f;
    float nav_h_v = igGetFrameHeight() * 1.35f;
    if (igButton("Back##vlither_top", (ImVec2){nav_w_v, nav_h_v})) {
      save_user_settings(us);
      g->curr_screen = TITLE_SCREEN;
      igPopFont();
      return;
    }
    igSameLine(0, style->ItemSpacing.x);
    if (igButton("Chat##vlither_top", (ImVec2){nav_w_v, nav_h_v})) {
      S.vlither_chat_open = true;
      us->vlither_chat_hud_visible = true;
      save_user_settings(us);
      S.focus_vlither_input = true;
    }
    igSeparator();

    /* Re-read the remaining region after the controls above.  Fill only the
       space that actually exists so the message box and player list stay on
       screen instead of extending past the bottom edge. */
    ImVec2 body_avail_v;
    igGetContentRegionAvail(&body_avail_v);
    bool wide_v = body_avail_v.x >= 720.0f;
    float body_h_v = body_avail_v.y;
    if (body_h_v < 1.0f) body_h_v = 1.0f;
    float msg_w = wide_v ? body_avail_v.x * 0.66f : body_avail_v.x;
    float players_w = wide_v ? body_avail_v.x - msg_w - style->ItemSpacing.x : body_avail_v.x;

    igBeginChild_Str("##vlither_chat_panel", (ImVec2){msg_w, wide_v ? body_h_v : body_h_v * 0.62f},
                     ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
    igSeparatorText("Vlither Chat");
    float input_h_v = igGetFrameHeight() + style->ItemSpacing.y;
    igBeginChild_Str("##vlither_panel_msgs", (ImVec2){0, -input_h_v},
                     ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    int vcount = vlither_chat_history_count();
    if (vcount == 0) {
      igTextDisabled(!chat_joined
                         ? "You left Vlither Global Chat."
                         : vlither_chat_connected()
                         ? "No Vlither messages yet."
                         : "Waiting for the Vlither chat server...");
    } else {
      for (int i = 0; i < vcount; ++i) {
        const char *sender = vlither_chat_history_nick(i);
        if (us->ntl_chat_timestamps) {
          char clock[6];
          ntl_history_clock(
              (time_t)(vlither_chat_history_time_ms(i) / 1000LL), clock);
          igTextDisabled("%s", clock);
          igSameLine(0, 6);
        }
        char sender_label[96];
        ntl_vlither_profile_label(sender_label, sizeof sender_label,
                                  vlither_chat_history_emoji(i), sender);
        igTextColored(ntl_vlither_text_color(
                          vlither_chat_history_color(i), sender),
                      "%s", sender_label);
        igSameLine(0, 6);
        igTextWrapped("%s", vlither_chat_history_text(i));
      }
    }
    if (vcount != S.last_vlither_history_count) {
      igSetScrollHereY(1.0f);
      S.last_vlither_history_count = vcount;
    }
    igEndChild();
    igBeginDisabled(!chat_joined);
    igSetNextItemWidth(-76);
    if (S.focus_vlither_input) {
      igSetKeyboardFocusHere(0);
      S.focus_vlither_input = false;
    }
    igPushStyleColor_Vec4(ImGuiCol_FrameBg, (ImVec4){0.08f, 0.10f, 0.14f, 0.28f});
    igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered, (ImVec4){0.12f, 0.16f, 0.22f, 0.40f});
    igPushStyleColor_Vec4(ImGuiCol_FrameBgActive, (ImVec4){0.14f, 0.20f, 0.28f, 0.48f});
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.95f, 0.97f, 1.0f, 0.92f});
    igPushStyleColor_Vec4(ImGuiCol_TextDisabled, (ImVec4){0.75f, 0.80f, 0.88f, 0.55f});
    igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 10.0f);
    igPushStyleVar_Float(ImGuiStyleVar_FrameBorderSize, 1.0f);
    igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){1.0f, 1.0f, 1.0f, 0.18f});
    bool v_enter = igInputTextWithHint(
        "##vlither_panel_input", "Message Vlither chat...", S.input,
        sizeof S.input, ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
    igPopStyleColor(1);
    igPopStyleVar(2);
    igPopStyleColor(5);
    igSameLine(0, 6);
    if (igButton("Send##vlither", (ImVec2){70, 0}) || v_enter)
      chat_submit_current(us);
    igEndDisabled();
    igEndChild();

    if (wide_v) igSameLine(0, style->ItemSpacing.x);
    igBeginChild_Str("##vlither_players_panel",
                     (ImVec2){players_w, wide_v ? body_h_v : body_h_v * 0.36f},
                     ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    igSeparatorText("Online Vlither Players");
    igPushFont(u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR],
               u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR]->LegacySize *
                   us->friends_panel_zoom);
    int pcount = vlither_chat_player_count();
    if (!pcount) {
      igTextDisabled("No Vlither players online.");
    } else {
      for (int i = 0; i < pcount; ++i) {
        ntl_draw_vlither_player_row(us, i, true);
        igSpacing();
      }
    }
    igPopFont();
    igEndChild();

    igPopFont();
    return;
  }

  igText("NTL Chat");
  igSameLine(0, 12);
  if (!us->ntl_enabled)
    igTextColored((ImVec4){1.0f, 0.55f, 0.25f, 1.0f}, "Disabled");
  else if (strlen(us->ntl_auth_key) < 16 || strlen(us->ntl_team_id) < 16)
    igTextColored((ImVec4){1.0f, 0.35f, 0.35f, 1.0f}, "Credentials required");
  else {
    double now = mg_millis() / 1000.0;
    bool recently_connected = S.last_success > 0 && now - S.last_success < NTL_CONNECTED_GRACE_SECONDS;
    if (recently_connected) {
      igTextColored((ImVec4){0.35f, 1.0f, 0.5f, 1.0f},
                    S.consecutive_failures > 0
                        ? "Connected - reconnecting (%d online)"
                        : "Connected - %d online",
                    S.count);
    } else if (S.request_active)
      igTextColored((ImVec4){1.0f, 0.85f, 0.3f, 1.0f}, "Connecting...");
    else if (S.consecutive_failures > 0)
      igTextColored((ImVec4){1.0f, 0.68f, 0.25f, 1.0f},
                    "Reconnecting automatically...");
    else if (!S.last_request_ok && S.last_http_status != 0)
      igTextColored((ImVec4){1.0f, 0.35f, 0.35f, 1.0f}, "Server error HTTP %d", S.last_http_status);
    else
      igTextColored((ImVec4){1.0f, 0.55f, 0.25f, 1.0f}, "Waiting for NTL server");
  }
  igSeparator();

  float footer_h = igGetFrameHeight() * 1.8f + style->ItemSpacing.y * 2.0f;
  float body_h = avail.y - footer_h - igGetFrameHeight() - style->ItemSpacing.y * 2.0f;
  if (body_h < 260) body_h = 260;
  bool wide = avail.x >= 760.0f;
  float left_w = wide ? avail.x * 0.40f : avail.x;
  float right_w = wide ? avail.x - left_w - style->ItemSpacing.x : avail.x;

  igBeginChild_Str("##ntl_setup", (ImVec2){left_w, wide ? body_h : body_h * 0.48f},
                   ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
  igSeparatorText("Chat connection");
  igCheckbox("Enable NTL chat", &us->ntl_enabled);
  if (igCheckbox("Show NTL in-game chat", &S.ntl_chat_open)) {
    us->ntl_chat_hud_visible = S.ntl_chat_open;
    save_user_settings(us);
  }
  igCheckbox("Show NTL players", &S.players_open);
  igCheckbox("Show FPS & ping in player list##ntl", &us->ntl_show_player_stats);
  ntl_draw_friends_zoom_control(us, "##ntl_friends_zoom");
  igTextDisabled("Shows FPS/ping from Vlither Android and NTL clients that publish NTL performance details.");

  igSpacing();
  igSeparatorText("Saved teams");
  const char *profile_preview = "Select saved team";
  if (us->ntl_active_team_profile >= 0 &&
      us->ntl_active_team_profile < us->ntl_team_profile_count)
    profile_preview = us->ntl_team_profiles[us->ntl_active_team_profile].name;
  igSetNextItemWidth(-1);
  if (igBeginCombo("##ntl_saved_team", profile_preview, ImGuiComboFlags_None)) {
    for (int i = 0; i < us->ntl_team_profile_count; ++i) {
      bool selected = i == us->ntl_active_team_profile;
      if (igSelectable_Bool(us->ntl_team_profiles[i].name, selected,
                            ImGuiSelectableFlags_None, (ImVec2){0, 0})) {
        ntl_load_profile(us, i);
        us->ntl_enabled = true;
        save_user_settings(us);
      }
      if (selected) igSetItemDefaultFocus();
    }
    igEndCombo();
  }
  igSetNextItemWidth(-1);
  igInputTextWithHint("##ntl_profile_name", "Team name (required to save)",
                      S.profile_name, sizeof S.profile_name,
                      ImGuiInputTextFlags_None, NULL, NULL);
  igSetNextItemWidth(-1);
  igInputTextWithHint("##ntl_tid", "NTL Team ID", us->ntl_team_id,
                      sizeof(us->ntl_team_id), ImGuiInputTextFlags_None, NULL, NULL);
  igSetNextItemWidth(-1);
  igInputTextWithHint("##ntl_auth", "NTL Auth Key", us->ntl_auth_key,
                      sizeof(us->ntl_auth_key), ImGuiInputTextFlags_Password, NULL, NULL);
  igTextWrapped("Enter your NTL Team ID and Auth Key to use chat, teammate positions, and the player list.");
  igTextDisabled("NTL name: %s", us->nickname[0] ? us->nickname : "Vlither");
  igTextDisabled("A saved-team profile is created only when Team name is not empty.");
  igTextDisabled("Saved teams: %d/%d", us->ntl_team_profile_count,
                 MAX_NTL_TEAM_PROFILES);
  igSpacing();
  if (igButton("Save team", (ImVec2){150, 0})) {
    if (ntl_normalize_profile_name(S.profile_name) && us->ntl_team_id[0] &&
        us->ntl_auth_key[0]) {
      int index = ntl_find_profile(us, S.profile_name);
      if (index < 0 && us->ntl_team_profile_count < MAX_NTL_TEAM_PROFILES)
        index = us->ntl_team_profile_count++;
      if (index >= 0) {
        ntl_team_profile *profile = &us->ntl_team_profiles[index];
        strncpy(profile->name, S.profile_name, sizeof profile->name - 1);
        profile->name[sizeof profile->name - 1] = 0;
        strncpy(profile->team_id, us->ntl_team_id, sizeof profile->team_id - 1);
        profile->team_id[sizeof profile->team_id - 1] = 0;
        strncpy(profile->auth_key, us->ntl_auth_key, sizeof profile->auth_key - 1);
        profile->auth_key[sizeof profile->auth_key - 1] = 0;
        us->ntl_active_team_profile = index;
        us->ntl_enabled = true;
        ntl_sync_active_credentials(us);
        save_user_settings(us);
        S.next_poll = 0;
      }
    }
  }
  igSameLine(0, 8);
  if (igButton("Delete team", (ImVec2){150, 0}) &&
      us->ntl_active_team_profile >= 0 &&
      us->ntl_active_team_profile < us->ntl_team_profile_count) {
    int index = us->ntl_active_team_profile;
    for (int i = index; i + 1 < us->ntl_team_profile_count; ++i)
      us->ntl_team_profiles[i] = us->ntl_team_profiles[i + 1];
    us->ntl_team_profile_count--;
    memset(&us->ntl_team_profiles[us->ntl_team_profile_count], 0,
           sizeof us->ntl_team_profiles[0]);
    us->ntl_active_team_profile = -1;
    S.profile_name[0] = 0;
    save_user_settings(us);
  }
  igSpacing();
  if (igButton("Reconnect now", (ImVec2){150, 0})) {
    ntl_reset_feed(true);
    S.next_poll = 0;
  }
  igSameLine(0, 8);
  if (igButton("Clear credentials", (ImVec2){150, 0})) {
    us->ntl_team_id[0] = 0;
    us->ntl_auth_key[0] = 0;
    us->ntl_active_team_profile = -1;
    S.profile_name[0] = 0;
    us->ntl_enabled = false;
    ntl_sync_active_credentials(us);
    S.pending_msg[0] = 0;
  }
  igEndChild();

  if (wide) igSameLine(0, style->ItemSpacing.x);

  igBeginChild_Str("##ntl_team_and_chat",
                   (ImVec2){right_w, wide ? body_h : body_h * 0.50f},
                   ImGuiChildFlags_Borders, ImGuiWindowFlags_None);

  if (igBeginTabBar("##ntl_panel_tabs", ImGuiTabBarFlags_None)) {
    ImGuiTabItemFlags chat_flags = S.select_chat_tab
                                       ? ImGuiTabItemFlags_SetSelected
                                       : ImGuiTabItemFlags_None;
    if (igBeginTabItem("Team chat", NULL, chat_flags)) {
      S.select_chat_tab = false;
      igTextDisabled("Only new messages received after joining are shown.");
      igSameLine(0, 10);
      if (igSmallButton("Clear chat")) {
        memset(S.history, 0, sizeof S.history);
        S.history_count = 0;
        S.history_start = 0;
        S.scroll_chat_bottom = false;
      }
      igSeparator();

      float input_h = igGetFrameHeight() + style->ItemSpacing.y;
      igBeginChild_Str("##ntl_panel_msgs", (ImVec2){0, -input_h},
                       ImGuiChildFlags_None,
                       ImGuiWindowFlags_AlwaysVerticalScrollbar);
      if (S.history_count == 0) {
        igTextDisabled("No new team messages yet.");
      } else {
        for (int n = 0; n < S.history_count; ++n) {
          int i = (S.history_start + n) % NTL_CHAT_HISTORY_MAX;
          if (us->ntl_chat_timestamps) {
            char clock[6];
            ntl_history_clock(S.history[i].time, clock);
            igTextDisabled("%s", clock);
            igSameLine(0, 6);
          }
          igTextColored((ImVec4){0.35f, 0.85f, 1.0f, 1.0f}, "%s",
                        S.history[i].nick);
          igSameLine(0, 6);
          igTextWrapped("%s", S.history[i].text);
        }
      }
      if (S.scroll_chat_bottom) {
        igSetScrollHereY(1.0f);
        S.scroll_chat_bottom = false;
      }
      igEndChild();

      igSetNextItemWidth(-76);
      igPushStyleColor_Vec4(ImGuiCol_FrameBg, (ImVec4){0.08f, 0.10f, 0.14f, 0.28f});
      igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered, (ImVec4){0.12f, 0.16f, 0.22f, 0.40f});
      igPushStyleColor_Vec4(ImGuiCol_FrameBgActive, (ImVec4){0.14f, 0.20f, 0.28f, 0.48f});
      igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.95f, 0.97f, 1.0f, 0.92f});
      igPushStyleColor_Vec4(ImGuiCol_TextDisabled, (ImVec4){0.75f, 0.80f, 0.88f, 0.55f});
      igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 10.0f);
      igPushStyleVar_Float(ImGuiStyleVar_FrameBorderSize, 1.0f);
      igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){1.0f, 1.0f, 1.0f, 0.18f});
      bool enter = igInputTextWithHint(
          "##ntl_panel_input", "Message team...", S.input, sizeof S.input,
          ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
      igPopStyleColor(1);
      igPopStyleVar(2);
      igPopStyleColor(5);
      igSameLine(0, 6);
      if (igButton("Send", (ImVec2){70, 0}) || enter) ntl_queue_message(us);
      igEndTabItem();
    }

    if (igBeginTabItem("Players & minimap", NULL, ImGuiTabItemFlags_None)) {
      igSeparatorText("Minimap player dots");
      igCheckbox("Show teammates on minimap", &us->ntl_show_teammates);
      igCheckbox("Show teammate names", &us->ntl_marker_labels);
      igTextWrapped("Change how large player dots appear on the minimap.");
      igText("My dot size");
      igSetNextItemWidth(-1.0f);
      if (igSliderFloat("##own_marker_size", &us->own_marker_size, 2.0f, 28.0f,
                        "%.1f px", ImGuiSliderFlags_AlwaysClamp)) {
        /* live preview while dragging */
      }
      if (igIsItemDeactivatedAfterEdit()) save_user_settings(us);
      igText("Shape / color");
      igSetNextItemWidth(140);
      igCombo_Str_arr("##own_marker_shape", &us->own_marker_shape,
                      (const char*[]){"Circle", "Diamond", "Triangle"}, 3, -1);
      igSameLine(0, 8);
      igColorEdit4("##own_marker_color", us->own_marker_color,
                   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
      igSpacing();
      igText("Other players' dot size");
      igSetNextItemWidth(-1.0f);
      igSliderFloat("##team_marker_size", &us->ntl_marker_size, 2.0f, 28.0f,
                    "%.1f px", ImGuiSliderFlags_AlwaysClamp);
      if (igIsItemDeactivatedAfterEdit()) save_user_settings(us);
      igText("Shape / color");
      igSetNextItemWidth(140);
      igCombo_Str_arr("##team_marker_shape", &us->ntl_marker_shape,
                      (const char*[]){"Circle", "Diamond", "Triangle"}, 3, -1);
      igSameLine(0, 8);
      igColorEdit4("##team_marker_color", us->ntl_marker_color,
                   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
      igSpacing();

      igSeparatorText("Online players");
      igBeginChild_Str("##ntl_members", (ImVec2){0, 0},
                       ImGuiChildFlags_None,
                       ImGuiWindowFlags_AlwaysVerticalScrollbar);
      igPushFont(u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR],
                 u->imgui_data.mono_font_bold[FONT_SIZE_REGULAR]->LegacySize *
                     us->friends_panel_zoom);
      if (S.count == 0) {
        igTextDisabled("No NTL players received yet.");
      } else {
        for (int i = 0; i < S.count; ++i) {
          ntl_member *m = &S.members[i];
          ntl_draw_team_player_row(us, m, true);
          bool same = same_server(m->srv, us->server_address);
          char status[96];
          if (m->score > 0)
            snprintf(status, sizeof status, "%s | Score %d",
                     same ? "Same server" : "Other server", m->score);
          else
            snprintf(status, sizeof status, "%s",
                     same ? "Same server" : "Other server");
          ntl_draw_wrapped_color(
              same ? (ImVec4){0.35f, 1.0f, 0.5f, 1.0f}
                   : (ImVec4){0.65f, 0.65f, 0.65f, 1.0f},
              status);
          igSpacing();
        }
      }
      igPopFont();
      igEndChild();
      igEndTabItem();
    }
    igEndTabBar();
  }
  igEndChild();

  float btn_w = (avail.x - style->ItemSpacing.x) * 0.5f;
  if (igButton("Save", (ImVec2){btn_w, igGetFrameHeight() * 1.6f})) {
    ntl_sync_active_credentials(us);
    save_user_settings(us);
    S.next_poll = 0;
  }
  igSameLine(0, style->ItemSpacing.x);
  if (igButton("Back", (ImVec2){btn_w, igGetFrameHeight() * 1.6f})) {
    save_user_settings(us);
    g->curr_screen = TITLE_SCREEN;
  }

  igPopFont();
}
