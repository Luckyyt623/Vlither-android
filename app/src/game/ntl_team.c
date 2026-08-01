#include "ntl_team.h"
#include "ntl_tags.h"
#include "vlither_tags.h"
#include "../user.h"
#include "../external/mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|(ImU32)(R))
#endif
#define NTL_MAX_TEAM 64
#define NTL_URL_MAX 2048
#define NTL_POLL_SECONDS 1.0
#define NTL_RETRY_BASE_SECONDS 1.0
#define NTL_RETRY_MAX_SECONDS 6.0
#define NTL_REQUEST_TIMEOUT_SECONDS 7.0
#define NTL_CONNECTED_GRACE_SECONDS 35.0
#define NTL_CHAT_HISTORY_MAX 80
#define NTL_CHAT_SNAPSHOT_MAX 1024

typedef struct {
  char nick[64], msg[NTL_CHAT_SNAPSHOT_MAX], srv[64], dt[128];
  float x, y;
  int sid, score, rank, tg;
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
  char input[256]; bool chat_open; bool players_open;
  char request_path[NTL_URL_MAX]; int last_http_status; bool last_request_ok;
  int consecutive_failures;
  struct { char nick[64], text[256]; double time; } history[NTL_CHAT_HISTORY_MAX];
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
  char last_presence_server[MAX_IPV4_LEN + 1];
  bool last_presence_playing;
} ntl_state;
static ntl_state S;

static float ntl_clampf(float v, float lo, float hi) {
  if (hi < lo) hi = lo;
  return v < lo ? lo : (v > hi ? hi : v);
}
static bool ntl_feed_is_fresh(void);
static void ntl_queue_message(const user_settings *us);
static const char *ntl_clean_name(const char *name);
static void ntl_reset_feed(bool clear_history);
static void ntl_ensure_client_id(user_settings *us);
static void ntl_schedule_retry(double now);
static bool same_server(const char *a, const char *b);
static size_t ntl_append_utf8(char *out, size_t cap, size_t n,
                              unsigned int cp);

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
  S.history[idx].time = mg_millis() / 1000.0;
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
    S.chat_open = true;
    return false;
  }

  strncpy(S.input, text, sizeof S.input - 1);
  S.input[sizeof S.input - 1] = 0;
  S.chat_open = true;
  us->ntl_chat_minimized = false;
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
      ntl_normalize_chat_text(m->nick, sizeof m->nick);
      ntl_decode_chat_snapshot(m->msg, sizeof m->msg);
      ntl_normalize_chat_text(m->dt, sizeof m->dt);
      m->x = (float)field_num(p, e, "valx", 0);
      m->y = (float)field_num(p, e, "valy", 0);
      m->sid = (int)field_num(p, e, "sid", -1);
      m->score = (int)field_num(p, e, "score", 0);
      m->rank = (int)field_num(p, e, "rank", 0);
      m->tg = (int)field_num(p, e, "tg", -1);

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
    struct mg_tls_opts tls = {.skip_verification = 1};
    mg_tls_init(c, &tls);
    mg_printf(c,
              "GET %s HTTP/1.1\r\nHost: ntl-slither.com\r\n"
              "User-Agent: Vlither/4.1\r\n"
              "Accept: application/json,text/plain,*/*\r\n"
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
  const char *presence_server = local ? us->ipv4 : "_GAME_MENU_";
  /* Match the official NTL client: publish the authoritative world
     coordinates, not the short-lived render correction offsets (fx/fy).
     Sending fx/fy can make the team marker jump ahead/behind after packets. */
  float x = local ? local->xx : 0.0f;
  float y = local ? local->yy : 0.0f;
  int sid = local ? (int)local->ntl_id : -1;
  int score = local ? g->data.score : 0;
  int rank = local ? g->data.rank : 0;

  /* Keep one message in flight until a successful response. A newly typed
     message can wait in pending_msg without overwriting the retrying one. */
  if (!S.inflight_msg[0] && S.pending_msg[0]) {
    strncpy(S.inflight_msg, S.pending_msg, sizeof S.inflight_msg - 1);
    S.inflight_msg[sizeof S.inflight_msg - 1] = 0;
    S.pending_msg[0] = 0;
  }

  char nick[192];
  char msg[768];
  char srv[192];
  urlenc(nick, sizeof nick, raw_nick);
  urlenc(msg, sizeof msg, S.inflight_msg);
  urlenc(srv, sizeof srv, presence_server);

  strncpy(S.request_team_id, us->ntl_team_id, sizeof S.request_team_id - 1);
  S.request_team_id[sizeof S.request_team_id - 1] = 0;
  strncpy(S.request_auth_key, us->ntl_auth_key, sizeof S.request_auth_key - 1);
  S.request_auth_key[sizeof S.request_auth_key - 1] = 0;
  strncpy(S.request_client_id, us->ntl_client_id, sizeof S.request_client_id - 1);
  S.request_client_id[sizeof S.request_client_id - 1] = 0;

  snprintf(
      S.request_path, sizeof S.request_path,
      "/slither/ntlplay-mt.php?auth=%s&tid=%s&nick=%s&score=%d"
      "&valx=%.0f&valy=%.0f&bot=false&sos=false&food=false&srv=%s"
      "&sid=%d&msg=%s&rank=%d&dt=Vlither&cs=%d&tg=%d&ver=4.1&tlm=&di=1000",
      us->ntl_auth_key, us->ntl_team_id, nick, score, x, y, srv, sid,
      msg, rank, local ? local->accessory : 0, us->ntl_tag_id);

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
  S.chat_open = true;
  S.select_chat_tab = true;
  tuser_data *u = env ? env->usr : NULL;
  if (u && u->usrs.ntl_active_team_profile >= 0 &&
      u->usrs.ntl_active_team_profile < u->usrs.ntl_team_profile_count) {
    ntl_team_profile *profile =
        &u->usrs.ntl_team_profiles[u->usrs.ntl_active_team_profile];
    strncpy(S.profile_name, profile->name, sizeof S.profile_name - 1);
    S.profile_name[sizeof S.profile_name - 1] = 0;
  }
#ifdef ANDROID
  S.players_open = false;
#else
  S.players_open = true;
#endif
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
  const char *presence_server = playing ? us->ipv4 : "_GAME_MENU_";
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
  ImU32 border = IM_COL32(0, 0, 0, 210);
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
    ImU32 col = igColorConvertFloat4ToU32((ImVec4){
        us->own_marker_color[0], us->own_marker_color[1],
        us->own_marker_color[2], us->own_marker_color[3]});
    ntl_draw_marker(dl, p, us->own_marker_size, us->own_marker_shape, col);
  }

  if (!us->ntl_enabled || !us->ntl_show_teammates || !ntl_feed_is_fresh())
    return;

  ImU32 team_col = igColorConvertFloat4ToU32((ImVec4){
      us->ntl_marker_color[0], us->ntl_marker_color[1],
      us->ntl_marker_color[2], us->ntl_marker_color[3]});
  for (int i = 0; i < S.count; ++i) {
    ntl_member *m = &S.members[i];
    if (!same_server(m->srv, us->ipv4) ||
        ntl_member_is_local(m, us, local))
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
      if (!same_server(other->srv, us->ipv4) ||
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
    ntl_draw_marker(dl, p, us->ntl_marker_size, us->ntl_marker_shape, team_col);

    if (us->ntl_marker_labels) {
      const char *name = ntl_clean_name(m->nick);
      igPushFont(u->imgui_data.mono_font[FONT_SIZE_SMALL],
                 u->imgui_data.mono_font[FONT_SIZE_SMALL]->LegacySize);
      ImVec2 text_size;
      igCalcTextSize(&text_size, name, NULL, false, -1.0f);
      ImVec2 text_pos = {p.x - text_size.x * 0.5f,
                         p.y + us->ntl_marker_size + 2.0f};
      ImDrawList_AddText_Vec2(dl, text_pos, IM_COL32(255, 255, 255, 225),
                              name, NULL);
      igPopFont();
    }
  }
}

void ntl_team_consume_ui_touch(tenv *env) {
  (void)env;
  /* Android input ownership now prevents NTL-window touches from entering
     gameplay at ACTION_DOWN. Preserve any separate trackpad finger. */
}

void ntl_team_draw(tenv *env) {
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;
  if (!us->ntl_enabled || g->conn != CONNECTED) return;

  ImGuiViewport *vp = igGetMainViewport();
  ImGuiStyle *style = igGetStyle();
  float scale = vp->Size.x < 900.0f ? 0.92f : 1.0f;
  float chat_w = vp->Size.x < 900.0f ? vp->Size.x * 0.54f : 430.0f;
  float chat_h = vp->Size.y < 650.0f ? vp->Size.y * 0.43f : 285.0f;

  igPushFont(u->imgui_data.regular_font[us->ui_font_size],
             u->imgui_data.regular_font[us->ui_font_size]->LegacySize);
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 8.0f);
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 1.0f);
#ifdef ANDROID
  igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){10.0f, 9.0f});
#endif
  igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4){0.025f,0.035f,0.05f,0.88f});
  igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.12f,0.62f,0.78f,0.55f});

#ifdef ANDROID
  float chat_max_w = fmaxf(120.0f, vp->WorkSize.x * 0.92f);
  float chat_max_h = fmaxf(100.0f, vp->WorkSize.y * 0.88f);
  float chat_min_w = fminf(260.0f, chat_max_w);
  float chat_min_h = fminf(190.0f, chat_max_h);
  bool chat_minimized = us->ntl_chat_minimized;
  float compact_h = fminf(chat_max_h,
                          igGetFrameHeight() * 2.2f +
                              style->WindowPadding.y * 2.0f +
                              style->ItemSpacing.y * 2.0f + 12.0f);
  chat_w = ntl_clampf(us->ntl_chat_rel_w * vp->WorkSize.x,
                      chat_min_w, chat_max_w);
  float expanded_chat_h = ntl_clampf(us->ntl_chat_rel_h * vp->WorkSize.y,
                                     chat_min_h, chat_max_h);
  chat_h = chat_minimized ? compact_h : expanded_chat_h;
  float chat_x = vp->WorkPos.x + us->ntl_chat_rel_x * vp->WorkSize.x;
  float chat_y = vp->WorkPos.y + us->ntl_chat_rel_y * vp->WorkSize.y;
  chat_x = ntl_clampf(chat_x, vp->WorkPos.x,
                      vp->WorkPos.x + vp->WorkSize.x - chat_w);
  chat_y = ntl_clampf(chat_y, vp->WorkPos.y,
                      vp->WorkPos.y + vp->WorkSize.y - chat_h);
  igSetNextWindowPos((ImVec2){chat_x, chat_y}, ImGuiCond_Appearing,
                     (ImVec2){0,0});
  ImGuiCond chat_size_cond = (chat_minimized || S.chat_restore_size ||
                              (S.chat_h > 0.0f && S.chat_h < chat_min_h))
                                 ? ImGuiCond_Always
                                 : ImGuiCond_Appearing;
  igSetNextWindowSize((ImVec2){chat_w, chat_h}, chat_size_cond);
  S.chat_restore_size = false;
  if (!chat_minimized)
    igSetNextWindowSizeConstraints((ImVec2){chat_min_w, chat_min_h},
                                   (ImVec2){chat_max_w, chat_max_h}, NULL, NULL);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoSavedSettings;
  if (chat_minimized) flags |= ImGuiWindowFlags_NoResize;
#else
  bool chat_minimized = us->ntl_chat_minimized;
  igSetNextWindowPos((ImVec2){vp->WorkPos.x + 14.0f, vp->WorkPos.y + 14.0f},
                     ImGuiCond_FirstUseEver, (ImVec2){0,0});
  if (chat_minimized)
    chat_h = igGetFrameHeight() * 2.2f + style->WindowPadding.y * 2.0f +
             style->ItemSpacing.y * 2.0f + 12.0f;
  igSetNextWindowSize((ImVec2){chat_w, chat_h},
                      chat_minimized ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
  if (chat_minimized) flags |= ImGuiWindowFlags_NoResize;
#endif
  if (igBegin("NTL Chat##game_hud", &S.chat_open, flags)) {
#ifdef ANDROID
    ImVec2 chat_pos_now; igGetWindowPos(&chat_pos_now);
    ImVec2 chat_size_now; igGetWindowSize(&chat_size_now);
    if (S.chat_w > 0.0f &&
        (fabsf(S.chat_x - chat_pos_now.x) > 0.5f ||
         fabsf(S.chat_y - chat_pos_now.y) > 0.5f ||
         fabsf(S.chat_w - chat_size_now.x) > 0.5f ||
         (!chat_minimized && fabsf(S.chat_h - chat_size_now.y) > 0.5f)))
      S.layout_dirty = true;
    S.chat_x = chat_pos_now.x; S.chat_y = chat_pos_now.y;
    S.chat_w = chat_size_now.x; S.chat_h = chat_size_now.y;
    android_ui_capture_rect(S.chat_x, S.chat_y, S.chat_x + S.chat_w,
                            S.chat_y + S.chat_h);
    if (vp->WorkSize.x > 0.0f && vp->WorkSize.y > 0.0f) {
      us->ntl_chat_rel_x = (chat_pos_now.x - vp->WorkPos.x) / vp->WorkSize.x;
      us->ntl_chat_rel_y = (chat_pos_now.y - vp->WorkPos.y) / vp->WorkSize.y;
      us->ntl_chat_rel_w = chat_size_now.x / vp->WorkSize.x;
      if (!chat_minimized)
        us->ntl_chat_rel_h = chat_size_now.y / vp->WorkSize.y;
    }
#endif
    bool connected = ntl_feed_is_fresh();
    igTextColored(connected ? (ImVec4){0.35f,1.0f,0.5f,1.0f} : (ImVec4){1.0f,0.68f,0.25f,1.0f},
                  connected ? "Connected" : "Connecting...");
    igSameLine(0,10);
    igTextDisabled("%d online", S.count);
    igSameLine(0,10);
    if (igSmallButton(chat_minimized ? "Open chat" : "Minimize")) {
      us->ntl_chat_minimized = !chat_minimized;
      S.chat_restore_size = chat_minimized;
      save_user_settings(us);
    }
    igSameLine(0,10);
    if (igSmallButton(S.players_open ? "Hide players" : "Show players")) S.players_open = !S.players_open;
    if (!chat_minimized) {
      igSeparator();

      float input_h = igGetFrameHeight() + style->ItemSpacing.y;
      igBeginChild_Str("##ntl_chat_history", (ImVec2){0,-input_h}, ImGuiChildFlags_None,
                       ImGuiWindowFlags_AlwaysVerticalScrollbar);
      if (S.history_count == 0) igTextDisabled("No team messages yet.");
      for (int n=0;n<S.history_count;n++) {
        int i=(S.history_start + n) % NTL_CHAT_HISTORY_MAX;
        igTextColored((ImVec4){0.35f,0.85f,1.0f,1.0f}, "%s", S.history[i].nick);
        igSameLine(0,6);
        igTextWrapped("%s", S.history[i].text);
      }
      if (S.scroll_chat_bottom) { igSetScrollHereY(1.0f); S.scroll_chat_bottom=false; }
      igEndChild();

      igSetNextItemWidth(-76.0f * scale);
      bool enter=igInputTextWithHint("##ntl_game_input","Message team...",S.input,sizeof S.input,
                                     ImGuiInputTextFlags_EnterReturnsTrue,NULL,NULL);
      igSameLine(0,6);
      if (igButton("Send",(ImVec2){70.0f*scale,0})||enter) ntl_queue_message(us);
    }
  }
  igEnd();

  if (S.players_open) {
    ImGuiWindowFlags player_flags = ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoSavedSettings;
    float pw = vp->Size.x < 900.0f ? vp->Size.x * 0.38f : 315.0f;
    float ph = vp->Size.y < 650.0f ? vp->Size.y * 0.43f : 285.0f;
#ifdef ANDROID
    float players_max_w = fmaxf(110.0f, vp->WorkSize.x * 0.82f);
    float players_max_h = fmaxf(100.0f, vp->WorkSize.y * 0.88f);
    float players_min_w = fminf(230.0f, players_max_w);
    float players_min_h = fminf(180.0f, players_max_h);
    pw = ntl_clampf(us->ntl_players_rel_w * vp->WorkSize.x,
                    players_min_w, players_max_w);
    ph = ntl_clampf(us->ntl_players_rel_h * vp->WorkSize.y,
                    players_min_h, players_max_h);
    float players_x = vp->WorkPos.x + us->ntl_players_rel_x * vp->WorkSize.x;
    float players_y = vp->WorkPos.y + us->ntl_players_rel_y * vp->WorkSize.y;
    players_x = ntl_clampf(players_x, vp->WorkPos.x,
                           vp->WorkPos.x + vp->WorkSize.x - pw);
    players_y = ntl_clampf(players_y, vp->WorkPos.y,
                           vp->WorkPos.y + vp->WorkSize.y - ph);
    igSetNextWindowPos((ImVec2){players_x, players_y},
                       ImGuiCond_Appearing, (ImVec2){0,0});
    igSetNextWindowSize((ImVec2){pw,ph}, ImGuiCond_Appearing);
    igSetNextWindowSizeConstraints((ImVec2){players_min_w, players_min_h},
                                   (ImVec2){players_max_w, players_max_h},
                                   NULL, NULL);
#else
    igSetNextWindowPos((ImVec2){vp->WorkPos.x + vp->WorkSize.x - pw - 14.0f,
                                vp->WorkPos.y + 14.0f},
                       ImGuiCond_FirstUseEver, (ImVec2){0,0});
    igSetNextWindowSize((ImVec2){pw,ph}, ImGuiCond_FirstUseEver);
#endif
    if (igBegin("NTL Players##game_hud", &S.players_open, player_flags)) {
#ifdef ANDROID
      ImVec2 players_pos_now; igGetWindowPos(&players_pos_now);
      ImVec2 players_size_now; igGetWindowSize(&players_size_now);
      if (S.players_w > 0.0f &&
          (fabsf(S.players_x - players_pos_now.x) > 0.5f ||
           fabsf(S.players_y - players_pos_now.y) > 0.5f ||
           fabsf(S.players_w - players_size_now.x) > 0.5f ||
           fabsf(S.players_h - players_size_now.y) > 0.5f))
        S.layout_dirty = true;
      S.players_x = players_pos_now.x; S.players_y = players_pos_now.y;
      S.players_w = players_size_now.x; S.players_h = players_size_now.y;
      android_ui_capture_rect(S.players_x, S.players_y,
                              S.players_x + S.players_w,
                              S.players_y + S.players_h);
      if (vp->WorkSize.x > 0.0f && vp->WorkSize.y > 0.0f) {
        us->ntl_players_rel_x = (players_pos_now.x - vp->WorkPos.x) / vp->WorkSize.x;
        us->ntl_players_rel_y = (players_pos_now.y - vp->WorkPos.y) / vp->WorkSize.y;
        us->ntl_players_rel_w = players_size_now.x / vp->WorkSize.x;
        us->ntl_players_rel_h = players_size_now.y / vp->WorkSize.y;
      }
#endif
      int same_count=0;
      for(int i=0;i<S.count;i++) if(same_server(S.members[i].srv,us->ipv4)) same_count++;
      igText("Players"); igSameLine(0,8); igTextDisabled("%d same server",same_count);
      igSeparator();
      igBeginChild_Str("##ntl_player_list",(ImVec2){0,0},ImGuiChildFlags_None,ImGuiWindowFlags_AlwaysVerticalScrollbar);
      if(S.count==0) igTextDisabled("Waiting for NTL players...");
      for(int i=0;i<S.count;i++){
        ntl_member *m=&S.members[i]; bool same=same_server(m->srv,us->ipv4);
        igTextColored(same?(ImVec4){0.35f,1.0f,0.5f,1.0f}:(ImVec4){0.62f,0.66f,0.72f,1.0f},"%s", ntl_clean_name(m->nick));
        if(m->score>0){igSameLine(0,8);igTextDisabled("%d",m->score);}
        if(m->rank>0){igSameLine(0,8);igTextDisabled("#%d",m->rank);}
        igTextDisabled("%s",same?"Same server":(m->srv[0]?m->srv:"Game menu"));
        if(m->dt[0]) igTextDisabled("%s",m->dt);
        igSeparator();
      }
      igEndChild();
    }
    igEnd();
  }

#ifdef ANDROID
  if (S.layout_dirty && igIsMouseReleased_Nil(0)) {
    save_user_settings(us);
    S.layout_dirty = false;
  }
#endif

  igPopStyleColor(2);
#ifdef ANDROID
  igPopStyleVar(3);
#else
  igPopStyleVar(2);
#endif
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

void ntl_team_panel(tenv *env) {
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;
  ImGuiStyle *style = igGetStyle();
  ImVec2 avail;
  igGetContentRegionAvail(&avail);

  igPushFont(u->imgui_data.regular_font[us->ui_font_size],
             u->imgui_data.regular_font[us->ui_font_size]->LegacySize);

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
  igCheckbox("Show in-game chat", &S.chat_open);
  igCheckbox("Minimize in-game chat", &us->ntl_chat_minimized);
  igCheckbox("Show player list", &S.players_open);

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
      bool enter = igInputTextWithHint(
          "##ntl_panel_input", "Message team...", S.input, sizeof S.input,
          ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
      igSameLine(0, 6);
      if (igButton("Send", (ImVec2){70, 0}) || enter) ntl_queue_message(us);
      igEndTabItem();
    }

    if (igBeginTabItem("Players & minimap", NULL, ImGuiTabItemFlags_None)) {
      igSeparatorText("Minimap teammates");
      igCheckbox("Show teammates on minimap", &us->ntl_show_teammates);
      igCheckbox("Show teammate names", &us->ntl_marker_labels);
      igText("My dot");
      igSameLine(0, 8);
      igSetNextItemWidth(120);
      igCombo_Str_arr("##own_marker_shape", &us->own_marker_shape,
                      (const char*[]){"Circle", "Diamond", "Triangle"}, 3, -1);
      igSameLine(0, 8);
      igSetNextItemWidth(120);
      igSliderFloat("##own_marker_size", &us->own_marker_size, 2.0f, 14.0f,
                    "%.1f px", ImGuiSliderFlags_AlwaysClamp);
      igSameLine(0, 8);
      igColorEdit4("##own_marker_color", us->own_marker_color,
                   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
      igText("Team dots");
      igSameLine(0, 8);
      igSetNextItemWidth(120);
      igCombo_Str_arr("##team_marker_shape", &us->ntl_marker_shape,
                      (const char*[]){"Circle", "Diamond", "Triangle"}, 3, -1);
      igSameLine(0, 8);
      igSetNextItemWidth(120);
      igSliderFloat("##team_marker_size", &us->ntl_marker_size, 2.0f, 14.0f,
                    "%.1f px", ImGuiSliderFlags_AlwaysClamp);
      igSameLine(0, 8);
      igColorEdit4("##team_marker_color", us->ntl_marker_color,
                   ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
      igSpacing();

      igSeparatorText("Online players");
      igBeginChild_Str("##ntl_members", (ImVec2){0, 0},
                       ImGuiChildFlags_None,
                       ImGuiWindowFlags_AlwaysVerticalScrollbar);
      if (S.count == 0) {
        igTextDisabled("No NTL players received yet.");
      } else {
        for (int i = 0; i < S.count; ++i) {
          ntl_member *m = &S.members[i];
          bool same = same_server(m->srv, us->ipv4);
          igText("%s", ntl_clean_name(m->nick));
          igSameLine(0, 10);
          igTextColored(
              same ? (ImVec4){0.35f, 1.0f, 0.5f, 1.0f}
                   : (ImVec4){0.65f, 0.65f, 0.65f, 1.0f},
              same ? "Same server" : "Other server");
          if (m->score > 0) {
            igSameLine(0, 10);
            igTextDisabled("Score %d", m->score);
          }
          igTextDisabled("Server: %s", m->srv[0] ? m->srv : "Unknown");
          igSpacing();
        }
      }
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
