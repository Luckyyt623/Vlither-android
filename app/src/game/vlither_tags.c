#include "vlither_tags.h"
#include "tag_follow.h"

#include "ntl_team.h"
#include "../user.h"
#include "../rendering/texture.h"
#include "../cimgui/cimgui_impl.h"
#ifdef ANDROID
#include "../android_jni.h"
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VLITHER_TAG_BACKEND_URL "http://139.84.170.60:10000"
#define VLITHER_TAG_MAX_ENTRIES 256
#define VLITHER_TAG_RECONNECT_SECONDS 4.0
#define VLITHER_TAG_HEARTBEAT_SECONDS 1.0
#define VLITHER_TAG_HTTP_TIMEOUT_SECONDS 15.0
#define VLITHER_TAG_MAX_ATLAS_BYTES (16u * 1024u * 1024u)
#define VLITHER_CHAT_HISTORY_MAX 80
#define VLITHER_CHAT_PLAYER_MAX 128
#define VLITHER_VOICE_ROOM_MAX 32
#define VLITHER_VOICE_MEMBER_MAX 24
#define VLITHER_VOICE_FRAME_MAX 2048
#define VLITHER_EVENT_MAX 24

typedef struct vlither_tag_atlas_entry {
  int id;
  float u0, v0, u1, v1;
  float aspect;
  float display_width;
  float display_height;
  float attach_x;
  float attach_y;
  char name[64];
} vlither_tag_atlas_entry;

typedef enum vlither_http_kind {
  VLITHER_HTTP_NONE = 0,
  VLITHER_HTTP_REDEEM,
  VLITHER_HTTP_ATLAS
} vlither_http_kind;

typedef struct vlither_chat_message {
  long long seq;
  char nick[64];
  char text[256];
  char server[96];
} vlither_chat_message;

typedef struct vlither_chat_player {
  char client_id[65];
  char nick[64];
  char server[96];
  char version[16];
  char voice_room_id[25];
  int snake_id;
  float x, y;
  int fps, ping;
  bool voice_enabled;
  bool voice_muted;
  bool voice_deafened;
} vlither_chat_player;

typedef struct vlither_voice_member {
  char client_id[65];
  char nick[64];
  bool host;
  bool muted;
  bool deafened;
} vlither_voice_member;

typedef struct vlither_voice_room {
  char id[25];
  char name[48];
  char host_client_id[65];
  bool locked;
  int members;
  int max_players;
  vlither_voice_member member_list[VLITHER_VOICE_MEMBER_MAX];
  int member_count;
} vlither_voice_room;

typedef struct vlither_event {
  char id[65];
  char name[97];
  char country[65];
  char prize[97];
  char rules[1601];
  char server_ip[MAX_IPV4_LEN + 1];
  long long start_at_ms;
  long long end_at_ms;
  long long remove_at_ms;
  bool interested;
  int interested_count;
} vlither_event;

typedef struct vlither_tags_state {
  bool ready;
  tenv *env;
  struct mg_mgr mgr;
  struct mg_connection *ws;
  struct mg_connection *http;
  vlither_http_kind http_kind;
  bool ws_open;
  bool hello_sent;
  double next_connect;
  double last_heartbeat;
  double http_started;
  char connected_base[192];
  char ws_url[224];
  char http_url[320];
  char pending_body[384];
  char status[224];
  char code_input[96];
  char atlas_version[80];
  char pending_atlas_version[80];
  vlither_tag_atlas_entry entries[VLITHER_TAG_MAX_ENTRIES];
  int entry_count;
  texture *atlas_tex;
  VkDescriptorSet atlas_ds;
  int mapped_count;
  vlither_chat_message chat[VLITHER_CHAT_HISTORY_MAX];
  int chat_count;
  int chat_start;
  long long last_chat_seq;
  vlither_chat_player players[VLITHER_CHAT_PLAYER_MAX];
  int player_count;

  vlither_event events[VLITHER_EVENT_MAX];
  int event_count;
  char event_status[192];
  long long event_server_now_ms;

  bool voice_enabled;
  bool voice_muted;
  bool voice_deafened;
  bool voice_muted_before_deafen;
  bool voice_capture_requested;
  double voice_capture_refresh_at;
  bool voice_in_room;
  bool voice_room_host;
  char voice_room_id[25];
  char voice_room_name[48];
  char voice_status[160];
  vlither_voice_room voice_rooms[VLITHER_VOICE_ROOM_MAX];
  int voice_room_count;
  vlither_voice_member voice_members[VLITHER_VOICE_MEMBER_MAX];
  int voice_member_count;
  unsigned long long voice_tx_frames;
  unsigned long long voice_rx_frames;
  int voice_listener_count;
} vlither_tags_state;

static vlither_tags_state S;

static void voice_apply_audio_state(void);
static bool send_voice_status(void);

static void set_status(const char *text) {
  if (!text) text = "";
  strncpy(S.status, text, sizeof S.status - 1);
  S.status[sizeof S.status - 1] = 0;
}

static void set_voice_status(const char *text) {
  if (!text) text = "";
  strncpy(S.voice_status, text, sizeof S.voice_status - 1);
  S.voice_status[sizeof S.voice_status - 1] = 0;
}

static snake *local_snake(void) {
  if (!S.env || !S.env->usr) return NULL;
  game_data *g = &S.env->usr->gdata;
  return get_snake(g, g->data.snake_id);
}

static void voice_password_hash_hex(char out[65], const char *password) {
  if (!out) return;
  out[0] = 0;
  if (!password || !password[0]) return;
  unsigned char digest[32];
  mg_sha256_ctx ctx;
  mg_sha256_init(&ctx);
  mg_sha256_update(&ctx, (const unsigned char *)password, strlen(password));
  mg_sha256_final(digest, &ctx);
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    out[i * 2] = hex[(digest[i] >> 4) & 15];
    out[i * 2 + 1] = hex[digest[i] & 15];
  }
  out[64] = 0;
}

static void json_escape(char *dst, size_t dst_size, const char *src) {
  size_t n = 0;
  if (!dst_size) return;
  for (; src && *src && n + 1 < dst_size; ++src) {
    unsigned char ch = (unsigned char)*src;
    if ((ch == '"' || ch == '\\') && n + 2 < dst_size) {
      dst[n++] = '\\';
      dst[n++] = (char)ch;
    } else if (ch >= 32) {
      dst[n++] = (char)ch;
    }
  }
  dst[n] = 0;
}

static bool normalize_base_url(const char *input, char *out, size_t out_size) {
  if (!input || !out || out_size < 16) return false;
  while (isspace((unsigned char)*input)) ++input;
  size_t len = strlen(input);
  while (len && isspace((unsigned char)input[len - 1])) --len;
  while (len && input[len - 1] == '/') --len;
  if (len < 10 || len >= out_size) return false;
  if (strncmp(input, "https://", 8) != 0 &&
      strncmp(input, "http://", 7) != 0)
    return false;
  memcpy(out, input, len);
  out[len] = 0;
  return true;
}

static bool make_url(const char *base, const char *path,
                     char *out, size_t out_size) {
  int n = snprintf(out, out_size, "%s%s", base, path);
  return n > 0 && n < (int)out_size;
}

static bool make_ws_url(const char *base, char *out, size_t out_size) {
  const char *rest = NULL;
  const char *scheme = NULL;
  if (!strncmp(base, "https://", 8)) {
    rest = base + 8;
    scheme = "wss://";
  } else if (!strncmp(base, "http://", 7)) {
    rest = base + 7;
    scheme = "ws://";
  } else {
    return false;
  }
  int n = snprintf(out, out_size, "%s%s/ws", scheme, rest);
  return n > 0 && n < (int)out_size;
}

static const vlither_tag_atlas_entry *find_entry(int id) {
  for (int i = 0; i < S.entry_count; ++i)
    if (S.entries[i].id == id) return &S.entries[i];
  return NULL;
}

static void clear_snake_mappings(void) {
  if (!S.env || !S.env->usr) return;
  game_data *g = &S.env->usr->gdata;
  int count = tdarray_length(g->data.snakes);
  for (int i = 0; i < count; ++i) g->data.snakes[i].vlither_tag_id = -1;
  S.mapped_count = 0;
}

static void apply_selected_tag(int id, const char *name) {
  if (!S.env || !S.env->usr) return;
  user_settings *us = &S.env->usr->usrs;
  us->vlither_tag_id = id;
  if (name) {
    strncpy(us->vlither_tag_name, name, sizeof us->vlither_tag_name - 1);
    us->vlither_tag_name[sizeof us->vlither_tag_name - 1] = 0;
  } else if (id < 0) {
    us->vlither_tag_name[0] = 0;
  }
  snake *me = local_snake();
  if (me) me->vlither_tag_id = id;
  save_user_settings(us);
}

static void replace_atlas(texture *new_tex, VkDescriptorSet new_ds,
                          const char *version) {
  if (!S.env || !S.env->ctx) return;
  if (S.atlas_ds) igImplVulkan_RemoveTexture(S.atlas_ds);
  if (S.atlas_tex) destroy_texture(S.env->ctx, S.atlas_tex);
  S.atlas_tex = new_tex;
  S.atlas_ds = new_ds;
  strncpy(S.atlas_version, version ? version : "",
          sizeof S.atlas_version - 1);
  S.atlas_version[sizeof S.atlas_version - 1] = 0;
}

static void parse_manifest(struct mg_str json) {
  vlither_tag_atlas_entry parsed[VLITHER_TAG_MAX_ENTRIES];
  int count = 0;
  for (int i = 0; i < VLITHER_TAG_MAX_ENTRIES; ++i) {
    char path[96];
    snprintf(path, sizeof path, "$.tags[%d].id", i);
    long id = mg_json_get_long(json, path, -1);
    if (id < 0) break;
    vlither_tag_atlas_entry e = {
        .id = (int)id, .aspect = 1.0f,
        .display_width = 0.0f, .display_height = 0.0f, .attach_x = 0.5f, .attach_y = 0.12f};
    double value = 0;
#define GET_FLOAT(field) \
    do { snprintf(path, sizeof path, "$.tags[%d]." #field, i); \
         if (mg_json_get_num(json, path, &value)) e.field = (float)value; } while (0)
    GET_FLOAT(u0); GET_FLOAT(v0); GET_FLOAT(u1); GET_FLOAT(v1); GET_FLOAT(aspect);
#undef GET_FLOAT
    snprintf(path, sizeof path, "$.tags[%d].displayWidth", i);
    if (mg_json_get_num(json, path, &value)) e.display_width = (float)value;
    snprintf(path, sizeof path, "$.tags[%d].displayHeight", i);
    if (mg_json_get_num(json, path, &value)) e.display_height = (float)value;
    snprintf(path, sizeof path, "$.tags[%d].attachX", i);
    if (mg_json_get_num(json, path, &value)) e.attach_x = (float)value;
    snprintf(path, sizeof path, "$.tags[%d].attachY", i);
    if (mg_json_get_num(json, path, &value)) e.attach_y = (float)value;
    snprintf(path, sizeof path, "$.tags[%d].name", i);
    char *name = mg_json_get_str(json, path);
    if (name) {
      strncpy(e.name, name, sizeof e.name - 1);
      e.name[sizeof e.name - 1] = 0;
      mg_free(name);
    }
    if (e.u1 <= e.u0 || e.v1 <= e.v0) continue;
    if (!isfinite(e.aspect) || e.aspect < 0.15f || e.aspect > 6.0f)
      e.aspect = 1.0f;
    if (!isfinite(e.display_width) || !isfinite(e.display_height) ||
        e.display_width < 8.0f || e.display_width > 160.0f ||
        e.display_height < 8.0f || e.display_height > 160.0f) {
      const float max_dim = 76.0f;
      if (e.aspect >= 1.0f) {
        e.display_width = max_dim;
        e.display_height = max_dim / e.aspect;
      } else {
        e.display_height = max_dim;
        e.display_width = max_dim * e.aspect;
      }
    }
    if (!isfinite(e.attach_x) || e.attach_x < 0.0f || e.attach_x > 1.0f)
      e.attach_x = 0.5f;
    if (!isfinite(e.attach_y) || e.attach_y < 0.0f || e.attach_y > 1.0f)
      e.attach_y = 0.12f;
    parsed[count++] = e;
  }
  if (count > 0 || mg_json_get(json, "$.tags", NULL) >= 0) {
    memcpy(S.entries, parsed, sizeof(parsed[0]) * (size_t)count);
    S.entry_count = count;
  }
}

static void request_atlas(const char *version);

static void copy_json_string(struct mg_str json, const char *path,
                             char *dst, size_t cap, const char *fallback) {
  if (!dst || cap == 0) return;
  dst[0] = 0;
  char *value = mg_json_get_str(json, path);
  if (value) {
    strncpy(dst, value, cap - 1);
    dst[cap - 1] = 0;
    mg_free(value);
  } else if (fallback) {
    strncpy(dst, fallback, cap - 1);
    dst[cap - 1] = 0;
  }
}

static void append_chat_message(long long seq, const char *nick,
                                const char *text, const char *server) {
  if (!text || !text[0]) return;
  if (seq > 0 && seq <= S.last_chat_seq) return;
  int idx;
  if (S.chat_count < VLITHER_CHAT_HISTORY_MAX) {
    idx = (S.chat_start + S.chat_count) % VLITHER_CHAT_HISTORY_MAX;
    ++S.chat_count;
  } else {
    idx = S.chat_start;
    S.chat_start = (S.chat_start + 1) % VLITHER_CHAT_HISTORY_MAX;
  }
  vlither_chat_message *m = &S.chat[idx];
  memset(m, 0, sizeof *m);
  m->seq = seq;
  strncpy(m->nick, nick && nick[0] ? nick : "Vlither", sizeof m->nick - 1);
  strncpy(m->text, text, sizeof m->text - 1);
  strncpy(m->server, server && server[0] ? server : "_GAME_MENU_",
          sizeof m->server - 1);
  if (seq > S.last_chat_seq) S.last_chat_seq = seq;
}

static void clear_chat_messages(void) {
  memset(S.chat, 0, sizeof S.chat);
  S.chat_count = 0;
  S.chat_start = 0;
  S.last_chat_seq = 0;
}

static void parse_chat_message(struct mg_str json, const char *base_path) {
  char path[128];
  snprintf(path, sizeof path, "%s.seq", base_path);
  long long seq = (long long)mg_json_get_long(json, path, 0);
  char nick[64], text[256], server[96];
  snprintf(path, sizeof path, "%s.nickname", base_path);
  copy_json_string(json, path, nick, sizeof nick, "Vlither");
  snprintf(path, sizeof path, "%s.text", base_path);
  copy_json_string(json, path, text, sizeof text, "");
  snprintf(path, sizeof path, "%s.server", base_path);
  copy_json_string(json, path, server, sizeof server, "_GAME_MENU_");
  append_chat_message(seq, nick, text, server);
}

static void parse_presence(struct mg_str json) {
  memset(S.players, 0, sizeof S.players);
  S.player_count = 0;
  for (int i = 0; i < VLITHER_CHAT_PLAYER_MAX; ++i) {
    char base[64], path[96];
    snprintf(base, sizeof base, "$.players[%d]", i);
    snprintf(path, sizeof path, "%s.nickname", base);
    char *probe = mg_json_get_str(json, path);
    if (!probe) break;
    vlither_chat_player *p = &S.players[S.player_count++];
    memset(p, 0, sizeof *p);
    strncpy(p->nick, probe[0] ? probe : "Vlither", sizeof p->nick - 1);
    mg_free(probe);
    snprintf(path, sizeof path, "%s.clientId", base);
    copy_json_string(json, path, p->client_id, sizeof p->client_id, "");
    snprintf(path, sizeof path, "%s.server", base);
    copy_json_string(json, path, p->server, sizeof p->server, "_GAME_MENU_");
    snprintf(path, sizeof path, "%s.version", base);
    copy_json_string(json, path, p->version, sizeof p->version, "?");
    snprintf(path, sizeof path, "%s.voiceRoomId", base);
    copy_json_string(json, path, p->voice_room_id,
                     sizeof p->voice_room_id, "");
    snprintf(path, sizeof path, "%s.voiceEnabled", base);
    mg_json_get_bool(json, path, &p->voice_enabled);
    snprintf(path, sizeof path, "%s.voiceMuted", base);
    mg_json_get_bool(json, path, &p->voice_muted);
    snprintf(path, sizeof path, "%s.voiceDeafened", base);
    mg_json_get_bool(json, path, &p->voice_deafened);
    if (p->voice_deafened) p->voice_muted = true;
    snprintf(path, sizeof path, "%s.snakeId", base);
    p->snake_id = (int)mg_json_get_long(json, path, -1);
    double number = 0.0;
    snprintf(path, sizeof path, "%s.x", base);
    p->x = mg_json_get_num(json, path, &number) ? (float)number : 0.0f;
    snprintf(path, sizeof path, "%s.y", base);
    p->y = mg_json_get_num(json, path, &number) ? (float)number : 0.0f;
    snprintf(path, sizeof path, "%s.fps", base);
    p->fps = (int)mg_json_get_long(json, path, -1);
    snprintf(path, sizeof path, "%s.ping", base);
    p->ping = (int)mg_json_get_long(json, path, -1);
  }
}

static long long json_millis(struct mg_str json, const char *path) {
  double value = 0.0;
  if (!mg_json_get_num(json, path, &value) || !isfinite(value) || value < 0.0)
    return 0;
  return (long long)value;
}

static void parse_events(struct mg_str json) {
  vlither_event previous[VLITHER_EVENT_MAX];
  int previous_count = S.event_count;
  if (previous_count > 0)
    memcpy(previous, S.events, sizeof(previous[0]) * (size_t)previous_count);
  memset(S.events, 0, sizeof S.events);
  S.event_count = 0;
  S.event_server_now_ms = json_millis(json, "$.serverNow");

  for (int i = 0; i < VLITHER_EVENT_MAX; ++i) {
    char base[64], path[128];
    snprintf(base, sizeof base, "$.events[%d]", i);
    snprintf(path, sizeof path, "%s.id", base);
    char *id = mg_json_get_str(json, path);
    if (!id) break;
    vlither_event *event = &S.events[S.event_count++];
    memset(event, 0, sizeof *event);
    strncpy(event->id, id, sizeof event->id - 1);
    mg_free(id);
    snprintf(path, sizeof path, "%s.name", base);
    copy_json_string(json, path, event->name, sizeof event->name, "Vlither Event");
    snprintf(path, sizeof path, "%s.country", base);
    copy_json_string(json, path, event->country, sizeof event->country, "Global");
    snprintf(path, sizeof path, "%s.prize", base);
    copy_json_string(json, path, event->prize, sizeof event->prize, "No prize");
    snprintf(path, sizeof path, "%s.rules", base);
    copy_json_string(json, path, event->rules, sizeof event->rules,
                     "Follow the host's instructions.");
    snprintf(path, sizeof path, "%s.serverIp", base);
    copy_json_string(json, path, event->server_ip, sizeof event->server_ip, "");
    snprintf(path, sizeof path, "%s.startAt", base);
    event->start_at_ms = json_millis(json, path);
    snprintf(path, sizeof path, "%s.endAt", base);
    event->end_at_ms = json_millis(json, path);
    snprintf(path, sizeof path, "%s.autoRemoveAt", base);
    event->remove_at_ms = json_millis(json, path);
    snprintf(path, sizeof path, "%s.interested", base);
    mg_json_get_bool(json, path, &event->interested);
    snprintf(path, sizeof path, "%s.interestedCount", base);
    event->interested_count = (int)mg_json_get_long(json, path, 0);
    if (event->interested_count < 0) event->interested_count = 0;
  }

#ifdef ANDROID
  for (int old = 0; old < previous_count; ++old) {
    if (!previous[old].interested || !previous[old].id[0]) continue;
    bool still_interested = false;
    for (int i = 0; i < S.event_count; ++i) {
      if (!strcmp(previous[old].id, S.events[i].id) && S.events[i].interested) {
        still_interested = true;
        break;
      }
    }
    if (!still_interested)
      android_jni_cancel_event_notification(previous[old].id);
  }

  const long long now_ms = (long long)time(NULL) * 1000LL;
  for (int i = 0; i < S.event_count; ++i) {
    vlither_event *event = &S.events[i];
    if (event->interested && event->start_at_ms > now_ms) {
      android_jni_schedule_event_notification(
          event->id, event->name, event->server_ip, event->start_at_ms);
    }
  }
#endif

  if (S.event_count == 0)
    strncpy(S.event_status, "No events are currently scheduled.",
            sizeof S.event_status - 1);
  else
    snprintf(S.event_status, sizeof S.event_status, "%d event%s available.",
             S.event_count, S.event_count == 1 ? "" : "s");
  S.event_status[sizeof S.event_status - 1] = 0;
}

static void parse_voice_rooms(struct mg_str json) {
  memset(S.voice_rooms, 0, sizeof S.voice_rooms);
  S.voice_room_count = 0;
  for (int i = 0; i < VLITHER_VOICE_ROOM_MAX; ++i) {
    char base[64], path[96];
    snprintf(base, sizeof base, "$.rooms[%d]", i);
    snprintf(path, sizeof path, "%s.id", base);
    char *id = mg_json_get_str(json, path);
    if (!id) break;
    vlither_voice_room *room = &S.voice_rooms[S.voice_room_count++];
    memset(room, 0, sizeof *room);
    strncpy(room->id, id, sizeof room->id - 1);
    mg_free(id);
    snprintf(path, sizeof path, "%s.name", base);
    copy_json_string(json, path, room->name, sizeof room->name, "Voice Room");
    snprintf(path, sizeof path, "%s.hostClientId", base);
    copy_json_string(json, path, room->host_client_id,
                     sizeof room->host_client_id, "");
    snprintf(path, sizeof path, "%s.locked", base);
    bool locked = false;
    mg_json_get_bool(json, path, &locked);
    room->locked = locked;
    snprintf(path, sizeof path, "%s.members", base);
    room->members = (int)mg_json_get_long(json, path, 0);
    snprintf(path, sizeof path, "%s.maxPlayers", base);
    room->max_players = (int)mg_json_get_long(json, path, 12);
    for (int j = 0; j < VLITHER_VOICE_MEMBER_MAX; ++j) {
      char member_base[96];
      snprintf(member_base, sizeof member_base, "%s.memberList[%d]", base, j);
      snprintf(path, sizeof path, "%s.nickname", member_base);
      char *nick = mg_json_get_str(json, path);
      if (!nick) break;
      vlither_voice_member *member = &room->member_list[room->member_count++];
      strncpy(member->nick, nick[0] ? nick : "Vlither",
              sizeof member->nick - 1);
      mg_free(nick);
      snprintf(path, sizeof path, "%s.clientId", member_base);
      copy_json_string(json, path, member->client_id,
                       sizeof member->client_id, "");
      snprintf(path, sizeof path, "%s.host", member_base);
      mg_json_get_bool(json, path, &member->host);
      snprintf(path, sizeof path, "%s.muted", member_base);
      mg_json_get_bool(json, path, &member->muted);
      snprintf(path, sizeof path, "%s.deafened", member_base);
      mg_json_get_bool(json, path, &member->deafened);
      if (member->deafened) member->muted = true;
    }
  }
}

static void parse_voice_state(struct mg_str json) {
  bool was_deafened = S.voice_deafened;
  /* Voice enabled is a local, persistent player preference. A delayed state
     packet from the backend must not turn it off after a refresh/reconnect;
     only vlither_voice_set_enabled(), called by the player's UI, may change
     this value. */
  if (S.env && S.env->usr)
    S.voice_enabled = S.env->usr->usrs.voice_chat_enabled;
  bool muted = S.voice_muted;
  bool deafened = S.voice_deafened;
  mg_json_get_bool(json, "$.muted", &muted);
  mg_json_get_bool(json, "$.deafened", &deafened);
  S.voice_deafened = deafened;
  S.voice_muted = muted || deafened;
  if (!deafened) S.voice_muted_before_deafen = S.voice_muted;
  char *room_id = mg_json_get_str(json, "$.room.id");
  S.voice_in_room = room_id && room_id[0];
  S.voice_room_host = false;
  S.voice_room_id[0] = 0;
  S.voice_room_name[0] = 0;
  memset(S.voice_members, 0, sizeof S.voice_members);
  S.voice_member_count = 0;
  if (S.voice_in_room) {
    strncpy(S.voice_room_id, room_id, sizeof S.voice_room_id - 1);
    S.voice_room_id[sizeof S.voice_room_id - 1] = 0;
    copy_json_string(json, "$.room.name", S.voice_room_name,
                     sizeof S.voice_room_name, "Voice Room");
    bool host = false;
    mg_json_get_bool(json, "$.room.host", &host);
    S.voice_room_host = host;

    for (int i = 0; i < VLITHER_VOICE_MEMBER_MAX; ++i) {
      char base[80], path[112];
      snprintf(base, sizeof base, "$.room.memberList[%d]", i);
      snprintf(path, sizeof path, "%s.nickname", base);
      char *nick = mg_json_get_str(json, path);
      if (!nick) break;
      vlither_voice_member *member =
          &S.voice_members[S.voice_member_count++];
      strncpy(member->nick, nick[0] ? nick : "Vlither",
              sizeof member->nick - 1);
      mg_free(nick);
      snprintf(path, sizeof path, "%s.clientId", base);
      copy_json_string(json, path, member->client_id,
                       sizeof member->client_id, "");
      snprintf(path, sizeof path, "%s.host", base);
      mg_json_get_bool(json, path, &member->host);
      snprintf(path, sizeof path, "%s.muted", base);
      mg_json_get_bool(json, path, &member->muted);
      snprintf(path, sizeof path, "%s.deafened", base);
      mg_json_get_bool(json, path, &member->deafened);
      if (member->deafened) member->muted = true;
    }
  }
  if (room_id) mg_free(room_id);
  char *message = mg_json_get_str(json, "$.message");
  if (message) {
    strncpy(S.voice_status, message, sizeof S.voice_status - 1);
    S.voice_status[sizeof S.voice_status - 1] = 0;
    mg_free(message);
  } else if (!S.voice_enabled) {
    strncpy(S.voice_status, "Voice chat is off.", sizeof S.voice_status - 1);
  } else if (S.voice_in_room) {
    snprintf(S.voice_status, sizeof S.voice_status, "Room: %s",
             S.voice_room_name[0] ? S.voice_room_name : "Voice Room");
  } else {
    strncpy(S.voice_status, "Proximity voice is active.",
            sizeof S.voice_status - 1);
  }
  S.voice_status[sizeof S.voice_status - 1] = 0;
#ifdef ANDROID
  if (S.voice_deafened && !was_deafened)
    android_jni_voice_stop_playback();
#endif
  voice_apply_audio_state();
}

static void handle_voice_binary(struct mg_str data) {
#ifdef ANDROID
  if (!S.voice_enabled || S.voice_deafened) return;
  const unsigned char *bytes = (const unsigned char *)data.buf;
  if (!bytes || data.len < 8 || bytes[0] != 0x56 || bytes[1] != 1 ||
      bytes[2] != 2) return;
  unsigned int gain_byte = bytes[3];
  size_t id_len = bytes[4];
  if (id_len < 1 || id_len > 64 || data.len <= 5 + id_len) return;
  size_t pcm_len = data.len - 5 - id_len;
  if (pcm_len < 2 || pcm_len > VLITHER_VOICE_FRAME_MAX || (pcm_len & 1u))
    return;
  char speaker[65];
  memcpy(speaker, bytes + 5, id_len);
  speaker[id_len] = 0;
  S.voice_rx_frames++;
  android_jni_voice_play_pcm(speaker, bytes + 5 + id_len, pcm_len,
                             (float)gain_byte / 255.0f);
#else
  (void)data;
#endif
}

static void handle_ws_json(struct mg_str json) {
  char *type = mg_json_get_str(json, "$.type");
  if (!type) return;
  if (!strcmp(type, "atlas")) {
    char *version = mg_json_get_str(json, "$.version");
    parse_manifest(json);
    if (version && version[0] &&
        (strcmp(version, S.atlas_version) || !S.atlas_tex))
      request_atlas(version);
    if (version) mg_free(version);
  } else if (!strcmp(type, "selected")) {
    int id = (int)mg_json_get_long(json, "$.tagId", -1);
    const vlither_tag_atlas_entry *entry = find_entry(id);
    apply_selected_tag(id, entry ? entry->name : NULL);
    if (id >= 0) {
      char msg[160];
      snprintf(msg, sizeof msg, "Vlither tag %d active%s%s.", id,
               entry && entry->name[0] ? ": " : "",
               entry && entry->name[0] ? entry->name : "");
      set_status(msg);
    }
  } else if (!strcmp(type, "chat_history")) {
    /* New Vlither backends do not retain chat history. Ignore this packet as
       well so a client never resurrects stale messages when connected to an
       older backend build. */
  } else if (!strcmp(type, "chat")) {
    parse_chat_message(json, "$");
  } else if (!strcmp(type, "presence")) {
    parse_presence(json);
  } else if (!strcmp(type, "events")) {
    parse_events(json);
  } else if (!strcmp(type, "event_error")) {
    char *message = mg_json_get_str(json, "$.message");
    if (message) {
      strncpy(S.event_status, message, sizeof S.event_status - 1);
      S.event_status[sizeof S.event_status - 1] = 0;
      mg_free(message);
    }
    if (S.ws && S.ws_open) {
      static const char refresh[] = "{\"type\":\"events_get\"}";
      mg_ws_send(S.ws, refresh, sizeof refresh - 1, WEBSOCKET_OP_TEXT);
    }
  } else if (!strcmp(type, "voice_rooms")) {
    parse_voice_rooms(json);
  } else if (!strcmp(type, "voice_state")) {
    parse_voice_state(json);
  } else if (!strcmp(type, "voice_tx_status")) {
    S.voice_listener_count = (int)mg_json_get_long(json, "$.listeners", 0);
  } else if (!strcmp(type, "voice_error")) {
    char *message = mg_json_get_str(json, "$.message");
    if (message) {
      strncpy(S.voice_status, message, sizeof S.voice_status - 1);
      S.voice_status[sizeof S.voice_status - 1] = 0;
      mg_free(message);
    }
  } else if (!strcmp(type, "state")) {
    clear_snake_mappings();
    if (!S.env || !S.env->usr) {
      mg_free(type);
      return;
    }
    game_data *g = &S.env->usr->gdata;
    for (int i = 0; i < 512; ++i) {
      char path[96];
      snprintf(path, sizeof path, "$.players[%d].snakeId", i);
      int snake_id = (int)mg_json_get_long(json, path, -1);
      if (snake_id < 0) break;
      snprintf(path, sizeof path, "$.players[%d].tagId", i);
      int tag_id = (int)mg_json_get_long(json, path, -1);
      snake *o = get_snake(g, snake_id);
      if (o && tag_id >= 0 && find_entry(tag_id)) {
        o->vlither_tag_id = tag_id;
        ++S.mapped_count;
      }
    }
    snake *me = local_snake();
    if (me && g->data.snake_id == me->id && me->vlither_tag_id < 0)
      me->vlither_tag_id = S.env->usr->usrs.vlither_tag_id;
  }
  mg_free(type);
}

static void ws_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != S.ws) return;
  if (ev == MG_EV_CONNECT && c->is_tls) {
    struct mg_tls_opts tls = {
        .name = mg_url_host(S.ws_url),
        .skip_verification = 1};
    mg_tls_init(c, &tls);
  } else if (ev == MG_EV_WS_OPEN) {
    S.ws_open = true;
    S.hello_sent = false;
    S.last_heartbeat = 0;
    /* Every connection is a fresh live-chat session. This also guarantees
       that a game/app reload or reconnect never resurrects old messages. */
    clear_chat_messages();
    set_status("Vlither services connected.");
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    if ((wm->flags & 0x0f) == WEBSOCKET_OP_BINARY)
      handle_voice_binary(wm->data);
    else
      handle_ws_json(wm->data);
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (S.ws == c) {
      S.ws = NULL;
      S.ws_open = false;
      S.hello_sent = false;
      S.player_count = 0;
      S.voice_listener_count = 0;
#ifdef ANDROID
      /* Do not keep recording or playing a stale queue while relay is down.
         Open mic resumes automatically after the socket reconnects. */
      android_jni_voice_set_capture(false);
      S.voice_capture_requested = false;
      android_jni_voice_stop_playback();
#endif
      S.next_connect = mg_millis() / 1000.0 + VLITHER_TAG_RECONNECT_SECONDS;
      clear_snake_mappings();
      set_status("Vlither services reconnecting...");
    }
  }
}

static void send_identity(const char *type) {
  if (!S.ws || !S.ws_open || !S.env || !S.env->usr) return;
  user_settings *us = &S.env->usr->usrs;
  game_data *g = &S.env->usr->gdata;
  snake *me = local_snake();
  bool playing = me && g->conn == CONNECTED &&
                 g->curr_screen == PLAYING && !g->preview_active;
  const char *server_name = playing && us->ipv4[0] ? us->ipv4 : "_GAME_MENU_";
  int snake_id = playing ? me->id : -1;
  float x = playing ? me->xx : 0.0f;
  float y = playing ? me->yy : 0.0f;
  int fps = playing && g->data.fps > 0 ? g->data.fps : -1;
  int ping = playing && g->data.ping > 0 ? g->data.ping : -1;

  char client[32], server[192], nick[96], version[32];
  json_escape(client, sizeof client, us->ntl_client_id);
  json_escape(server, sizeof server, server_name);
  json_escape(nick, sizeof nick, us->nickname[0] ? us->nickname : "Vlither");
  json_escape(version, sizeof version, APP_VERSION);
  char json[768];
  int n = snprintf(json, sizeof json,
                   "{\"type\":\"%s\",\"clientId\":\"%s\","
                   "\"server\":\"%s\",\"snakeId\":%d,\"nickname\":\"%s\","
                   "\"version\":\"%s\",\"x\":%.1f,\"y\":%.1f,"
                   "\"fps\":%d,\"ping\":%d,\"voiceEnabled\":%s,"
                   "\"voiceMuted\":%s,\"voiceDeafened\":%s}",
                   type, client, server, snake_id, nick, version, x, y, fps, ping,
                   S.voice_enabled ? "true" : "false",
                   (S.voice_muted || S.voice_deafened) ? "true" : "false",
                   S.voice_deafened ? "true" : "false");
  if (n > 0 && n < (int)sizeof json) {
    mg_ws_send(S.ws, json, (size_t)n, WEBSOCKET_OP_TEXT);
    S.hello_sent = true;
    S.last_heartbeat = mg_millis() / 1000.0;
  }
}

static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != S.http) return;
  if (ev == MG_EV_CONNECT) {
    if (c->is_tls) {
      struct mg_tls_opts tls = {
          .name = mg_url_host(S.http_url),
          .skip_verification = 1};
      mg_tls_init(c, &tls);
    }
    struct mg_str host = mg_url_host(S.http_url);
    const char *uri = mg_url_uri(S.http_url);
    if (S.http_kind == VLITHER_HTTP_REDEEM) {
      mg_printf(c,
                "POST %s HTTP/1.1\r\nHost: %.*s\r\n"
                "User-Agent: Vlither/Tags\r\nAccept: application/json\r\n"
                "Content-Type: application/json\r\nContent-Length: %d\r\n"
                "Connection: close\r\n\r\n%s",
                uri, (int)host.len, host.buf, (int)strlen(S.pending_body),
                S.pending_body);
    } else if (S.http_kind == VLITHER_HTTP_ATLAS) {
      mg_printf(c,
                "GET %s HTTP/1.1\r\nHost: %.*s\r\n"
                "User-Agent: Vlither/Tags\r\nAccept: image/png\r\n"
                "Connection: close\r\n\r\n",
                uri, (int)host.len, host.buf);
    }
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    int status = mg_http_status(hm);
    if (S.http_kind == VLITHER_HTTP_REDEEM) {
      struct mg_str json = hm->body;
      bool ok = false;
      mg_json_get_bool(json, "$.ok", &ok);
      if (status >= 200 && status < 300 && ok) {
        int id = (int)mg_json_get_long(json, "$.tagId", -1);
        char *name = mg_json_get_str(json, "$.name");
        apply_selected_tag(id, name);
        if (id < 0) {
          set_status("Vlither tag disabled.");
          ntl_team_system_message("Vlither tag disabled.");
        } else {
          char msg[192];
          snprintf(msg, sizeof msg, "Vlither tag code accepted: %d%s%s.", id,
                   name && name[0] ? " - " : "", name && name[0] ? name : "");
          set_status(msg);
          ntl_team_system_message(msg);
        }
        if (name) mg_free(name);
      } else {
        char *error = mg_json_get_str(json, "$.error");
        char msg[224];
        snprintf(msg, sizeof msg, "Vlither tag code failed (HTTP %d)%s%s.",
                 status, error && error[0] ? ": " : "",
                 error && error[0] ? error : "");
        set_status(msg);
        ntl_team_system_message(msg);
        if (error) mg_free(error);
      }
    } else if (S.http_kind == VLITHER_HTTP_ATLAS) {
      if (status >= 200 && status < 300 && hm->body.len > 0 &&
          hm->body.len <= VLITHER_TAG_MAX_ATLAS_BYTES && S.env &&
          S.env->ctx && S.env->usr && S.env->usr->r) {
        texture *tex = create_mipmap_texture_from_memory(
            S.env->ctx, (const unsigned char *)hm->body.buf, hm->body.len);
        if (tex) {
          VkDescriptorSet ds = igImplVulkan_AddTexture(
              S.env->usr->r->linear_sampler, tex->view,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
          if (ds) {
            replace_atlas(tex, ds, S.pending_atlas_version);
            S.pending_atlas_version[0] = 0;
            set_status("Vlither tag atlas loaded.");
          } else {
            destroy_texture(S.env->ctx, tex);
            set_status("Vlither tag atlas descriptor allocation failed.");
          }
        } else {
          set_status("Vlither tag atlas image could not be decoded.");
        }
      } else {
        char msg[160];
        snprintf(msg, sizeof msg, "Vlither tag atlas download failed (HTTP %d).", status);
        set_status(msg);
      }
    }
    S.http = NULL;
    S.http_kind = VLITHER_HTTP_NONE;
    S.http_started = 0;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (S.http == c) {
      S.http = NULL;
      S.http_kind = VLITHER_HTTP_NONE;
      S.http_started = 0;
      set_status("Vlither tag request failed.");
    }
  }
}

static bool begin_http(vlither_http_kind kind, const char *url,
                       const char *body) {
  if (S.http || !url || !url[0]) return false;
  strncpy(S.http_url, url, sizeof S.http_url - 1);
  S.http_url[sizeof S.http_url - 1] = 0;
  if (body) {
    strncpy(S.pending_body, body, sizeof S.pending_body - 1);
    S.pending_body[sizeof S.pending_body - 1] = 0;
  } else {
    S.pending_body[0] = 0;
  }
  S.http_kind = kind;
  S.http = mg_http_connect(&S.mgr, S.http_url, http_cb, NULL);
  S.http_started = mg_millis() / 1000.0;
  if (!S.http) {
    S.http_kind = VLITHER_HTTP_NONE;
    S.http_started = 0;
    return false;
  }
  return true;
}

static void request_atlas(const char *version) {
  if (!version || !version[0]) return;
  strncpy(S.pending_atlas_version, version,
          sizeof S.pending_atlas_version - 1);
  S.pending_atlas_version[sizeof S.pending_atlas_version - 1] = 0;
  if (!S.env || !S.env->usr || S.http ||
      (S.atlas_tex && !strcmp(S.atlas_version, version)))
    return;
  const char *base = VLITHER_TAG_BACKEND_URL;
  char encoded[128];
  size_t n = mg_url_encode(version, strlen(version), encoded, sizeof encoded);
  if (!n) return;
  char path[180];
  snprintf(path, sizeof path, "/api/v1/atlas.png?v=%s", encoded);
  char url[320];
  if (!make_url(base, path, url, sizeof url)) return;
  begin_http(VLITHER_HTTP_ATLAS, url, NULL);
}

static const char *strip_vtag_prefix(const char *code) {
  if (!code) return "";
  while (isspace((unsigned char)*code)) ++code;
  if (!strncmp(code, "!vtag", 5) &&
      (!code[5] || isspace((unsigned char)code[5]))) {
    code += 5;
    while (isspace((unsigned char)*code)) ++code;
  }
  return code;
}

static bool redeem_code(const char *code) {
  if (!S.env || !S.env->usr) return false;
  user_settings *us = &S.env->usr->usrs;
  const char *base = VLITHER_TAG_BACKEND_URL;
  if (S.http) {
    set_status("Another Vlither tag request is already running.");
    return false;
  }
  const char *clean_code = strip_vtag_prefix(code);
  if (!clean_code[0]) {
    set_status("Enter a Vlither tag activation code.");
    ntl_team_system_message(S.status);
    return false;
  }
  char escaped_code[192], escaped_client[32];
  json_escape(escaped_code, sizeof escaped_code, clean_code);
  json_escape(escaped_client, sizeof escaped_client, us->ntl_client_id);
  char body[320];
  int body_len = snprintf(body, sizeof body,
                          "{\"clientId\":\"%s\",\"code\":\"%s\"}",
                          escaped_client, escaped_code);
  char url[256];
  if (body_len <= 0 || body_len >= (int)sizeof body ||
      !make_url(base, "/api/v1/redeem", url, sizeof url) ||
      !begin_http(VLITHER_HTTP_REDEEM, url, body)) {
    set_status("Could not start the Vlither tag request.");
    ntl_team_system_message(S.status);
    return false;
  }
  set_status("Checking the Vlither tag code...");
  return true;
}

void vlither_tags_init(tenv *env) {
  memset(&S, 0, sizeof S);
  S.env = env;
  if (env && env->usr)
    S.voice_enabled = env->usr->usrs.voice_chat_enabled;
  set_status("Enter your Vlither tag activation code.");
  strncpy(S.event_status, "Loading events...", sizeof S.event_status - 1);
  strncpy(S.voice_status,
          S.voice_enabled ? "Voice chat will reconnect automatically."
                          : "Voice chat is off.",
          sizeof S.voice_status - 1);
  mg_mgr_init(&S.mgr);
  S.ready = true;
}

void vlither_tags_update(tenv *env) {
  if (!S.ready) return;
  if (env) S.env = env;
  mg_mgr_poll(&S.mgr, 0);

  /* Open mic follows the voice status instead of the visible screen. This is
     what lets a room keep working while the panel is closed or gameplay is
     active. Muting/deafening and socket loss still stop capture immediately. */
  voice_apply_audio_state();
#ifdef ANDROID
  if (S.voice_enabled && !S.voice_muted && !S.voice_deafened &&
      S.ws && S.ws_open) {
    unsigned char pcm[VLITHER_VOICE_FRAME_MAX];
    size_t pcm_len = 0;
    int sent = 0;
    while (sent < 8 && android_jni_voice_poll_capture(
                           pcm, sizeof pcm, &pcm_len)) {
      if (pcm_len >= 2 && pcm_len <= VLITHER_VOICE_FRAME_MAX && !(pcm_len & 1u)) {
        unsigned char packet[VLITHER_VOICE_FRAME_MAX + 4];
        packet[0] = 0x56; packet[1] = 1; packet[2] = 1; packet[3] = 0;
        memcpy(packet + 4, pcm, pcm_len);
        size_t queued = mg_ws_send(S.ws, packet, pcm_len + 4,
                                   WEBSOCKET_OP_BINARY);
        if (queued > pcm_len + 4) S.voice_tx_frames++;
      }
      ++sent;
    }
  }
#endif
  double now = mg_millis() / 1000.0;
  if (S.http && S.http_started > 0 &&
      now - S.http_started > VLITHER_TAG_HTTP_TIMEOUT_SECONDS) {
    S.http->is_closing = 1;
    S.http = NULL;
    S.http_kind = VLITHER_HTTP_NONE;
    S.http_started = 0;
    set_status("Vlither tag request timed out.");
  }
  if (!S.http && S.pending_atlas_version[0] &&
      (!S.atlas_tex || strcmp(S.pending_atlas_version, S.atlas_version)))
    request_atlas(S.pending_atlas_version);
  if (!S.env || !S.env->usr) return;
  user_settings *us = &S.env->usr->usrs;
  const char *base = VLITHER_TAG_BACKEND_URL;
  game_data *g = &S.env->usr->gdata;
  snake *me = local_snake();
  bool in_game = me && g->conn == CONNECTED &&
                 g->curr_screen == PLAYING && !g->preview_active;
  if (!in_game) clear_snake_mappings();
  if (strcmp(S.connected_base, base)) {
    if (S.ws) S.ws->is_closing = 1;
    S.ws = NULL;
    S.ws_open = false;
    S.hello_sent = false;
    strncpy(S.connected_base, base, sizeof S.connected_base - 1);
    S.connected_base[sizeof S.connected_base - 1] = 0;
    make_ws_url(base, S.ws_url, sizeof S.ws_url);
    S.next_connect = 0;
    clear_snake_mappings();
  }
  if (!S.ws && now >= S.next_connect) {
    S.ws = mg_ws_connect(&S.mgr, S.ws_url, ws_cb, NULL,
                         "Origin: https://vlither.app\r\n");
    if (!S.ws) S.next_connect = now + VLITHER_TAG_RECONNECT_SECONDS;
  }
  if (S.ws_open && !S.hello_sent) send_identity("hello");
  if (S.ws_open && S.hello_sent &&
      now - S.last_heartbeat >= VLITHER_TAG_HEARTBEAT_SECONDS)
    send_identity("heartbeat");
  if (in_game && me && me->vlither_tag_id < 0 && us->vlither_tag_id >= 0)
    me->vlither_tag_id = us->vlither_tag_id;
}

void vlither_tags_destroy(tenv *env) {
  if (!S.ready) return;
#ifdef ANDROID
  android_jni_voice_set_capture(false);
  S.voice_capture_requested = false;
  android_jni_voice_stop_playback();
#endif
  tcontext *ctx = env ? env->ctx : (S.env ? S.env->ctx : NULL);
  if (S.atlas_ds) igImplVulkan_RemoveTexture(S.atlas_ds);
  if (S.atlas_tex && ctx) destroy_texture(ctx, S.atlas_tex);
  mg_mgr_free(&S.mgr);
  memset(&S, 0, sizeof S);
}

bool vlither_tags_handle_command(const char *input) {
  if (!input) return false;
  while (isspace((unsigned char)*input)) ++input;
  if (strncmp(input, "!vtag", 5) != 0 ||
      (input[5] && !isspace((unsigned char)input[5])))
    return false;
  input += 5;
  while (isspace((unsigned char)*input)) ++input;
  if (!*input || !strcmp(input, "status")) {
    ntl_team_system_message(S.status);
  } else if (!strcmp(input, "off") || !strcmp(input, "none")) {
    redeem_code("off");
  } else {
    redeem_code(input);
  }
  return true;
}

void vlither_tags_skin_panel(tenv *env) {
  if (!env || !env->usr) return;
  user_settings *us = &env->usr->usrs;
  igSeparatorText("Vlither-only tags");
  igTextWrapped(
      "These tags are visible only to Vlither players. Enter the activation "
      "code below.");
  igSeparator();
  if (us->vlither_tag_id >= 0)
    igText("Selected: %d%s%s", us->vlither_tag_id,
           us->vlither_tag_name[0] ? " - " : "",
           us->vlither_tag_name[0] ? us->vlither_tag_name : "");
  else
    igText("Selected: Off");
  igTextDisabled("Connection: %s",
                 S.ws_open ? "connected" : "offline/reconnecting");
  if (S.status[0]) igTextWrapped("%s", S.status);
  igInputTextWithHint("##vlither_code", "Code only, for example H123",
                      S.code_input, sizeof S.code_input,
                      ImGuiInputTextFlags_None, NULL, NULL);
  if (igButton("Activate code", (ImVec2){0, 0}))
    redeem_code(S.code_input);
  igSameLine(0, -1);
  if (igButton("Disable", (ImVec2){0, 0})) redeem_code("off");
  igSpacing();
  igTextWrapped("Contact Lucky to upload free tags.");
}

bool vlither_chat_connected(void) {
  return S.ready && S.ws_open;
}

bool vlither_chat_send_text(const char *text) {
  if (!S.ready || !S.ws || !S.ws_open || !text) return false;
  while (isspace((unsigned char)*text)) ++text;
  if (!text[0]) return false;
  char clean[241];
  size_t len = strlen(text);
  while (len && isspace((unsigned char)text[len - 1])) --len;
  if (len > 240) len = 240;
  memcpy(clean, text, len);
  clean[len] = 0;
  char escaped[512];
  json_escape(escaped, sizeof escaped, clean);
  char json[576];
  int n = snprintf(json, sizeof json,
                   "{\"type\":\"chat\",\"text\":\"%s\"}", escaped);
  if (n <= 0 || n >= (int)sizeof json) return false;
  mg_ws_send(S.ws, json, (size_t)n, WEBSOCKET_OP_TEXT);
  return true;
}

static const vlither_chat_message *chat_at(int index) {
  if (index < 0 || index >= S.chat_count) return NULL;
  int slot = (S.chat_start + index) % VLITHER_CHAT_HISTORY_MAX;
  return &S.chat[slot];
}

int vlither_chat_history_count(void) { return S.chat_count; }
const char *vlither_chat_history_nick(int index) {
  const vlither_chat_message *m = chat_at(index); return m ? m->nick : "";
}
const char *vlither_chat_history_text(int index) {
  const vlither_chat_message *m = chat_at(index); return m ? m->text : "";
}
const char *vlither_chat_history_server(int index) {
  const vlither_chat_message *m = chat_at(index); return m ? m->server : "";
}
int vlither_chat_player_count(void) { return S.player_count; }
const char *vlither_chat_player_nick(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].nick : "";
}
const char *vlither_chat_player_server(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].server : "";
}
const char *vlither_chat_player_version(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].version : "";
}
const char *vlither_chat_player_client_id(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].client_id : "";
}
int vlither_chat_player_snake_id(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].snake_id : -1;
}
float vlither_chat_player_x(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].x : 0.0f;
}
float vlither_chat_player_y(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].y : 0.0f;
}
int vlither_chat_player_fps(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].fps : -1;
}
int vlither_chat_player_ping(int index) {
  return index >= 0 && index < S.player_count ? S.players[index].ping : -1;
}
bool vlither_chat_player_voice_enabled(int index) {
  return index >= 0 && index < S.player_count && S.players[index].voice_enabled;
}
bool vlither_chat_player_voice_muted(int index) {
  return index >= 0 && index < S.player_count &&
         (S.players[index].voice_muted || S.players[index].voice_deafened);
}
bool vlither_chat_player_voice_deafened(int index) {
  return index >= 0 && index < S.player_count && S.players[index].voice_deafened;
}
const char *vlither_chat_player_voice_room_id(int index) {
  return index >= 0 && index < S.player_count
             ? S.players[index].voice_room_id : "";
}

static void vlither_normalize_server(char *out, size_t cap, const char *in) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (!in) return;
  while (isspace((unsigned char)*in)) ++in;
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
  out[n] = 0;
}

bool vlither_chat_is_snake_player(int snake_id, const char *server) {
  if (!S.ready || !S.ws_open || snake_id < 0 || !server || !server[0])
    return false;
  char wanted[96];
  vlither_normalize_server(wanted, sizeof wanted, server);
  if (!wanted[0]) return false;
  for (int i = 0; i < S.player_count; ++i) {
    if (S.players[i].snake_id != snake_id) continue;
    char have[96];
    vlither_normalize_server(have, sizeof have, S.players[i].server);
    if (have[0] && !strcmp(have, wanted)) return true;
  }
  return false;
}

static const vlither_event *event_at(int index) {
  return index >= 0 && index < S.event_count ? &S.events[index] : NULL;
}

int vlither_event_count(void) { return S.event_count; }
const char *vlither_event_id_at(int index) {
  const vlither_event *event = event_at(index); return event ? event->id : "";
}
const char *vlither_event_name_at(int index) {
  const vlither_event *event = event_at(index); return event ? event->name : "";
}
const char *vlither_event_country_at(int index) {
  const vlither_event *event = event_at(index); return event ? event->country : "";
}
const char *vlither_event_prize_at(int index) {
  const vlither_event *event = event_at(index); return event ? event->prize : "";
}
const char *vlither_event_rules_at(int index) {
  const vlither_event *event = event_at(index); return event ? event->rules : "";
}
const char *vlither_event_server_at(int index) {
  const vlither_event *event = event_at(index); return event ? event->server_ip : "";
}
long long vlither_event_start_at_ms(int index) {
  const vlither_event *event = event_at(index); return event ? event->start_at_ms : 0;
}
long long vlither_event_end_at_ms(int index) {
  const vlither_event *event = event_at(index); return event ? event->end_at_ms : 0;
}
long long vlither_event_remove_at_ms(int index) {
  const vlither_event *event = event_at(index); return event ? event->remove_at_ms : 0;
}
bool vlither_event_interested_at(int index) {
  const vlither_event *event = event_at(index); return event ? event->interested : false;
}
int vlither_event_interested_count_at(int index) {
  const vlither_event *event = event_at(index); return event ? event->interested_count : 0;
}
const char *vlither_event_status(void) { return S.event_status; }

int vlither_event_next_interested(void) {
  const long long now_ms = (long long)time(NULL) * 1000LL;
  int best = -1;
  long long best_start = 0;
  for (int i = 0; i < S.event_count; ++i) {
    const vlither_event *event = &S.events[i];
    if (!event->interested || !event->id[0] ||
        (event->remove_at_ms > 0 && event->remove_at_ms <= now_ms)) continue;
    if (event->start_at_ms <= now_ms) return i;
    if (best < 0 || event->start_at_ms < best_start) {
      best = i;
      best_start = event->start_at_ms;
    }
  }
  return best;
}

static bool send_event_json(const char *json) {
  if (!S.ready || !S.ws || !S.ws_open || !json || !json[0]) return false;
  if (!S.hello_sent) send_identity("hello");
  if (!S.hello_sent) return false;
  mg_ws_send(S.ws, json, strlen(json), WEBSOCKET_OP_TEXT);
  return true;
}

bool vlither_event_refresh(void) {
  return send_event_json("{\"type\":\"events_get\"}");
}

bool vlither_event_set_interested(int index, bool interested) {
  vlither_event *event = index >= 0 && index < S.event_count
                             ? &S.events[index] : NULL;
  if (!event || !event->id[0] || !S.ready || !S.ws || !S.ws_open) {
    strncpy(S.event_status, "Events service is reconnecting.",
            sizeof S.event_status - 1);
    S.event_status[sizeof S.event_status - 1] = 0;
    return false;
  }
  char escaped_id[160];
  json_escape(escaped_id, sizeof escaped_id, event->id);
  char json[256];
  int n = snprintf(json, sizeof json,
                   "{\"type\":\"event_interest\",\"eventId\":\"%s\","
                   "\"interested\":%s}", escaped_id,
                   interested ? "true" : "false");
  if (n <= 0 || n >= (int)sizeof json || !send_event_json(json)) return false;
  if (event->interested != interested) {
    event->interested = interested;
    event->interested_count += interested ? 1 : -1;
    if (event->interested_count < 0) event->interested_count = 0;
  }
#ifdef ANDROID
  if (interested && event->start_at_ms > (long long)time(NULL) * 1000LL) {
    android_jni_schedule_event_notification(
        event->id, event->name, event->server_ip, event->start_at_ms);
  } else if (!interested) {
    android_jni_cancel_event_notification(event->id);
  }
#endif
  snprintf(S.event_status, sizeof S.event_status,
           interested ? "Interested saved. Event reminder requested."
                      : "Interest removed and reminder cancelled.");
  return true;
}

static bool send_voice_json(const char *json) {
  if (!S.ready || !S.ws || !S.ws_open || !json || !json[0]) return false;
  /* A room command must never overtake the first identity packet. The backend
     cannot attach voice state to this socket until hello has been sent. */
  if (!S.hello_sent) send_identity("hello");
  if (!S.hello_sent) return false;
  mg_ws_send(S.ws, json, strlen(json), WEBSOCKET_OP_TEXT);
  return true;
}

bool vlither_voice_enabled(void) { return S.voice_enabled; }
bool vlither_voice_in_room(void) { return S.voice_in_room; }
bool vlither_voice_room_host(void) { return S.voice_room_host; }
bool vlither_voice_muted(void) { return S.voice_muted || S.voice_deafened; }
bool vlither_voice_deafened(void) { return S.voice_deafened; }
const char *vlither_voice_room_id(void) { return S.voice_room_id; }
const char *vlither_voice_room_name(void) { return S.voice_room_name; }
const char *vlither_voice_status(void) { return S.voice_status; }
int vlither_voice_room_count(void) { return S.voice_room_count; }
const char *vlither_voice_room_id_at(int index) {
  return index >= 0 && index < S.voice_room_count ? S.voice_rooms[index].id : "";
}
const char *vlither_voice_room_name_at(int index) {
  return index >= 0 && index < S.voice_room_count ? S.voice_rooms[index].name : "";
}
bool vlither_voice_room_locked_at(int index) {
  return index >= 0 && index < S.voice_room_count ? S.voice_rooms[index].locked : false;
}
int vlither_voice_room_members_at(int index) {
  return index >= 0 && index < S.voice_room_count ? S.voice_rooms[index].members : 0;
}
int vlither_voice_room_max_at(int index) {
  return index >= 0 && index < S.voice_room_count ? S.voice_rooms[index].max_players : 0;
}
int vlither_voice_room_member_count_at(int room_index) {
  return room_index >= 0 && room_index < S.voice_room_count
             ? S.voice_rooms[room_index].member_count
             : 0;
}
const char *vlither_voice_room_member_name_at(int room_index,
                                              int member_index) {
  if (room_index < 0 || room_index >= S.voice_room_count) return "";
  vlither_voice_room *room = &S.voice_rooms[room_index];
  return member_index >= 0 && member_index < room->member_count
             ? room->member_list[member_index].nick
             : "";
}
bool vlither_voice_room_member_host_at(int room_index, int member_index) {
  if (room_index < 0 || room_index >= S.voice_room_count) return false;
  vlither_voice_room *room = &S.voice_rooms[room_index];
  return member_index >= 0 && member_index < room->member_count
             ? room->member_list[member_index].host
             : false;
}
bool vlither_voice_room_member_muted_at(int room_index, int member_index) {
  if (room_index < 0 || room_index >= S.voice_room_count) return false;
  vlither_voice_room *room = &S.voice_rooms[room_index];
  return member_index >= 0 && member_index < room->member_count
             ? room->member_list[member_index].muted
             : false;
}
bool vlither_voice_room_member_deafened_at(int room_index, int member_index) {
  if (room_index < 0 || room_index >= S.voice_room_count) return false;
  vlither_voice_room *room = &S.voice_rooms[room_index];
  return member_index >= 0 && member_index < room->member_count
             ? room->member_list[member_index].deafened
             : false;
}
int vlither_voice_member_count(void) { return S.voice_member_count; }
const char *vlither_voice_member_name_at(int index) {
  return index >= 0 && index < S.voice_member_count
             ? S.voice_members[index].nick
             : "";
}
bool vlither_voice_member_host_at(int index) {
  return index >= 0 && index < S.voice_member_count
             ? S.voice_members[index].host
             : false;
}
bool vlither_voice_member_muted_at(int index) {
  return index >= 0 && index < S.voice_member_count
             ? S.voice_members[index].muted
             : false;
}
bool vlither_voice_member_deafened_at(int index) {
  return index >= 0 && index < S.voice_member_count
             ? S.voice_members[index].deafened
             : false;
}

unsigned long long vlither_voice_tx_frames(void) { return S.voice_tx_frames; }
unsigned long long vlither_voice_rx_frames(void) { return S.voice_rx_frames; }
int vlither_voice_listener_count(void) { return S.voice_listener_count; }
int vlither_voice_audio_state(void) {
#ifdef ANDROID
  return android_jni_voice_audio_state();
#else
  return 0;
#endif
}

bool vlither_voice_set_enabled(bool enabled) {
  /* Persist only an explicit player action. Server state packets never write
     this setting, so refreshing or reconnecting cannot silently disable
     Voice Chat. */
  if (S.env && S.env->usr) {
    user_settings *us = &S.env->usr->usrs;
    if (us->voice_chat_enabled != enabled) {
      us->voice_chat_enabled = enabled;
      save_user_settings(us);
    }
  }
  S.voice_enabled = enabled;
  S.voice_listener_count = 0;
  if (enabled) {
    S.voice_muted = false;
    S.voice_deafened = false;
    S.voice_muted_before_deafen = false;
  }
#ifdef ANDROID
  if (enabled) {
    /* Voice is open-mic. Request permission as part of the explicit Enable
       action, then start capture as soon as Android grants it. */
    android_jni_voice_prepare();
  } else {
    android_jni_voice_set_capture(false);
    S.voice_capture_requested = false;
    android_jni_voice_stop_playback();
  }
#endif
  char json[160];
  snprintf(json, sizeof json,
           "{\"type\":\"voice_enable\",\"enabled\":%s,"
           "\"muted\":%s,\"deafened\":%s}",
           enabled ? "true" : "false",
           vlither_voice_muted() ? "true" : "false",
           S.voice_deafened ? "true" : "false");
  bool ok = send_voice_json(json);
  if (enabled) {
    strncpy(S.voice_status, "Voice enabled. Open mic is active.",
            sizeof S.voice_status - 1);
    send_voice_json("{\"type\":\"voice_rooms\"}");
  } else {
    S.voice_in_room = false;
    S.voice_room_id[0] = 0;
    S.voice_room_name[0] = 0;
    S.voice_member_count = 0;
    memset(S.voice_members, 0, sizeof S.voice_members);
    strncpy(S.voice_status, "Voice chat is off.", sizeof S.voice_status - 1);
  }
  S.voice_status[sizeof S.voice_status - 1] = 0;
  voice_apply_audio_state();
  return ok;
}

bool vlither_voice_refresh_rooms(void) {
  return send_voice_json("{\"type\":\"voice_rooms\"}");
}

bool vlither_voice_create_room(const char *name, const char *password) {
  if (!S.voice_enabled) {
    set_voice_status("Enable Voice Chat before creating a room.");
    return false;
  }
  if (!S.ws || !S.ws_open) {
    set_voice_status("Voice server is reconnecting. Try again in a moment.");
    return false;
  }

  while (name && isspace((unsigned char)*name)) ++name;
  size_t name_len = name ? strlen(name) : 0;
  while (name_len > 0 && isspace((unsigned char)name[name_len - 1]))
    --name_len;
  if (name_len == 0) {
    set_voice_status("Enter a room name.");
    return false;
  }
  if (name_len > 36) name_len = 36;
  char clean_name[37], ename[128], pass_hash[65];
  memcpy(clean_name, name, name_len);
  clean_name[name_len] = 0;
  json_escape(ename, sizeof ename, clean_name);
  voice_password_hash_hex(pass_hash, password ? password : "");
  char json[384];
  int n = snprintf(json, sizeof json,
                   "{\"type\":\"voice_create\",\"name\":\"%s\","
                   "\"passwordHash\":\"%s\",\"maxPlayers\":12}",
                   ename, pass_hash);
  bool sent = n > 0 && n < (int)sizeof json && send_voice_json(json);
  set_voice_status(sent ? "Creating voice room..."
                        : "Could not send the room request. Reconnect and try again.");
  return sent;
}

bool vlither_voice_join_room(const char *room_id, const char *password) {
  if (!S.voice_enabled) {
    set_voice_status("Enable Voice Chat before joining a room.");
    return false;
  }
  if (!S.ws || !S.ws_open) {
    set_voice_status("Voice server is reconnecting. Try again in a moment.");
    return false;
  }
  if (!room_id || !room_id[0]) {
    set_voice_status("Select a voice room first.");
    return false;
  }
  char eroom[64], pass_hash[65];
  json_escape(eroom, sizeof eroom, room_id);
  voice_password_hash_hex(pass_hash, password ? password : "");
  char json[320];
  int n = snprintf(json, sizeof json,
                   "{\"type\":\"voice_join\",\"roomId\":\"%s\","
                   "\"passwordHash\":\"%s\"}", eroom, pass_hash);
  bool sent = n > 0 && n < (int)sizeof json && send_voice_json(json);
  set_voice_status(sent ? "Joining voice room..."
                        : "Could not send the join request. Reconnect and try again.");
  return sent;
}

bool vlither_voice_leave_room(void) {
  return send_voice_json("{\"type\":\"voice_leave\"}");
}

static bool send_voice_status(void) {
  char json[128];
  int n = snprintf(json, sizeof json,
                   "{\"type\":\"voice_status\",\"muted\":%s,"
                   "\"deafened\":%s}",
                   vlither_voice_muted() ? "true" : "false",
                   S.voice_deafened ? "true" : "false");
  return n > 0 && n < (int)sizeof json && send_voice_json(json);
}

static void voice_apply_audio_state(void) {
#ifdef ANDROID
  /* Keep the Android recorder self-healing while open mic is active.
     A periodic TRUE refresh restarts an OEM AudioRecord that died without
     requiring the player to toggle voice off/on. FALSE stops immediately. */
  bool should_capture = S.voice_enabled && !vlither_voice_muted() &&
                        !S.voice_deafened && S.ws && S.ws_open;
  double now = mg_millis() / 1000.0;
  if (!should_capture) {
    if (S.voice_capture_requested)
      android_jni_voice_set_capture(false);
    S.voice_capture_requested = false;
    S.voice_capture_refresh_at = 0.0;
  } else if (!S.voice_capture_requested || now >= S.voice_capture_refresh_at) {
    /* A low-frequency refresh lets Kotlin restart a dead OEM AudioRecord
       without making a JNI call on every rendered frame. */
    android_jni_voice_set_capture(true);
    S.voice_capture_requested = true;
    S.voice_capture_refresh_at = now + 0.5;
  }
#endif
}

bool vlither_voice_set_muted(bool muted) {
  if (!S.voice_enabled) return false;
  if (S.voice_deafened) {
    /* Remember the requested mic state for when speaker audio is restored.
       Effective mute remains on for the whole deafen period. */
    S.voice_muted_before_deafen = muted;
    S.voice_muted = true;
  } else {
    S.voice_muted = muted;
    S.voice_muted_before_deafen = muted;
  }
  voice_apply_audio_state();
  return send_voice_status();
}

bool vlither_voice_set_deafened(bool deafened) {
  if (!S.voice_enabled) return false;
  if (S.voice_deafened == deafened) return true;
  if (deafened) {
    S.voice_muted_before_deafen = S.voice_muted;
    S.voice_muted = true;
    S.voice_deafened = true;
#ifdef ANDROID
    android_jni_voice_stop_playback();
#endif
  } else {
    S.voice_deafened = false;
    S.voice_muted = S.voice_muted_before_deafen;
  }
  voice_apply_audio_state();
  return send_voice_status();
}

void vlither_tags_draw(tenv *env, snake *o, float alpha,
                       float mww2, float mhh2) {
  if (!env || !env->usr || !env->usr->r || !o || o->dead ||
      o->vlither_tag_id < 0 || alpha <= 0.01f || !S.atlas_ds)
    return;
  const vlither_tag_atlas_entry *entry = find_entry(o->vlither_tag_id);
  if (!entry) return;
  game_data *g = &env->usr->gdata;
  float custom_scale = env->usr->usrs.tag_size_scale;
  if (!isfinite(custom_scale) || custom_scale < 0.50f || custom_scale > 2.00f)
    custom_scale = 1.0f;
  float zoom_scale = env->usr->usrs.tag_size_with_zoom
                         ? GLM_MAX(0.58f, GLM_MIN(1.75f, o->sc * g->data.gsc))
                         : 1.0f;
  float body_scale = zoom_scale * custom_scale;
  float hx = mww2 + (o->xx + o->fx - g->data.view_xx) * g->data.gsc;
  float hy = mhh2 + (o->yy + o->fy - g->data.view_yy) * g->data.gsc;

  float follow_ang = tag_follow_angle(
      &o->vlither_tag_follow_ang, &o->vlither_tag_follow_mtm,
      &o->vlither_tag_follow_ready, o->ang, g->data.ctm, 8.0f);
  float head_back_x = -cosf(o->ang), head_back_y = -sinf(o->ang);
  float tag_back_x = -cosf(follow_ang), tag_back_y = -sinf(follow_ang);
  float tag_side_x = -tag_back_y, tag_side_y = tag_back_x;

  /* Build the image dimensions first, then place the center using the actual
     rendered tag height. This keeps different Vlither tag shapes at a stable
     distance from the snake instead of using one fixed offset for every image. */
  float width = entry->display_width * body_scale;
  float height = entry->display_height * body_scale;
  float hw = width * 0.5f, hh = height * 0.5f;

  float angle = follow_ang + PI * 0.5f;
  float ux = cosf(angle), uy = sinf(angle);
  float vx = -uy, vy = ux;

  /* Use backend-supplied attachment metadata so the antenna connects to the
     real visual shape, matching the NTL extension style more closely. */
  float attach_x = entry->attach_x;
  float attach_y = entry->attach_y;
  if (!isfinite(attach_x) || attach_x < 0.0f || attach_x > 1.0f) attach_x = 0.5f;
  if (!isfinite(attach_y) || attach_y < 0.0f || attach_y > 1.0f) attach_y = 0.12f;

  float distance = 28.0f * body_scale + height * 0.10f;
  float lateral = (o->ntl_tag_id >= 0)
                      ? -(8.0f * body_scale + width * 0.34f)
                      : 0.0f;
  ImVec2 tip = {hx + tag_back_x * distance + tag_side_x * lateral,
                hy + tag_back_y * distance + tag_side_y * lateral};
  float local_attach_x = (attach_x - 0.5f) * width;
  float local_attach_y = (attach_y - 0.5f) * height;
  ImVec2 center = {tip.x - (ux * local_attach_x + vx * local_attach_y),
                   tip.y - (uy * local_attach_x + vy * local_attach_y)};
  ImVec2 q1 = {center.x - ux * hw - vx * hh,
               center.y - uy * hw - vy * hh};
  ImVec2 q2 = {center.x + ux * hw - vx * hh,
               center.y + uy * hw - vy * hh};
  ImVec2 q3 = {center.x + ux * hw + vx * hh,
               center.y + uy * hw + vy * hh};
  ImVec2 q4 = {center.x - ux * hw + vx * hh,
               center.y - uy * hw + vy * hh};

  /* The first control point follows the head immediately; the far end follows
     the smoothed tag heading. This keeps the antenna attached while allowing
     it to bend naturally through fast turns. */
  ImVec2 p0 = {hx + head_back_x * 9.0f * body_scale,
               hy + head_back_y * 9.0f * body_scale};
  ImVec2 p1 = {p0.x + head_back_x * distance * 0.34f,
               p0.y + head_back_y * distance * 0.34f};
  ImVec2 p2 = {tip.x - tag_back_x * distance * 0.26f,
               tip.y - tag_back_y * distance * 0.26f};
  ImDrawList *dl = igGetWindowDrawList();
  ImDrawList_AddBezierCubic(
      dl, p0, p1, p2, tip,
      igColorConvertFloat4ToU32((ImVec4){0.02f, 0.08f, 0.06f, alpha * 0.92f}),
      6.0f * body_scale, 16);
  ImDrawList_AddBezierCubic(
      dl, p0, p1, p2, tip,
      igColorConvertFloat4ToU32((ImVec4){0.35f, 1.0f, 0.68f, alpha}),
      2.8f * body_scale, 16);
  ImDrawList_AddCircleFilled(
      dl, tip, 3.5f * body_scale,
      igColorConvertFloat4ToU32((ImVec4){0.55f, 1.0f, 0.78f, alpha}), 12);

  ImTextureRef tex = {NULL, (ImTextureID)S.atlas_ds};
  ImDrawList_AddImageQuad(
      dl, tex, q1, q2, q3, q4,
      (ImVec2){entry->u0, entry->v0}, (ImVec2){entry->u1, entry->v0},
      (ImVec2){entry->u1, entry->v1}, (ImVec2){entry->u0, entry->v1},
      igColorConvertFloat4ToU32((ImVec4){1, 1, 1, alpha}));
}
