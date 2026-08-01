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

#define VLITHER_TAG_BACKEND_URL "http://139.84.170.60:10000"
#define VLITHER_TAG_MAX_ENTRIES 256
#define VLITHER_TAG_RECONNECT_SECONDS 4.0
#define VLITHER_TAG_HEARTBEAT_SECONDS 2.0
#define VLITHER_TAG_HTTP_TIMEOUT_SECONDS 15.0
#define VLITHER_TAG_MAX_ATLAS_BYTES (8u * 1024u * 1024u)

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
} vlither_tags_state;

static vlither_tags_state S;

static void set_status(const char *text) {
  if (!text) text = "";
  strncpy(S.status, text, sizeof S.status - 1);
  S.status[sizeof S.status - 1] = 0;
}

static snake *local_snake(void) {
  if (!S.env || !S.env->usr) return NULL;
  game_data *g = &S.env->usr->gdata;
  return get_snake(g, g->data.snake_id);
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
    set_status("Vlither tags connected.");
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    handle_ws_json(wm->data);
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (S.ws == c) {
      S.ws = NULL;
      S.ws_open = false;
      S.hello_sent = false;
      S.next_connect = mg_millis() / 1000.0 + VLITHER_TAG_RECONNECT_SECONDS;
      clear_snake_mappings();
      set_status("Vlither tags reconnecting...");
    }
  }
}

static void send_identity(const char *type) {
  if (!S.ws || !S.ws_open || !S.env || !S.env->usr) return;
  user_settings *us = &S.env->usr->usrs;
  snake *me = local_snake();
  if (!me) return;
  char client[32], server[192], nick[96];
  json_escape(client, sizeof client, us->ntl_client_id);
  json_escape(server, sizeof server, us->ipv4);
  json_escape(nick, sizeof nick, us->nickname[0] ? us->nickname : "Vlither");
  char json[512];
  int n = snprintf(json, sizeof json,
                   "{\"type\":\"%s\",\"clientId\":\"%s\","
                   "\"server\":\"%s\",\"snakeId\":%d,\"nickname\":\"%s\"}",
                   type, client, server, me->id, nick);
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
  set_status("Enter your Vlither tag activation code.");
  mg_mgr_init(&S.mgr);
  S.ready = true;
}

void vlither_tags_update(tenv *env) {
  if (!S.ready) return;
  if (env) S.env = env;
  mg_mgr_poll(&S.mgr, 0);
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
  if (!in_game) {
    if (S.ws) S.ws->is_closing = 1;
    S.ws = NULL;
    S.ws_open = false;
    S.hello_sent = false;
    S.connected_base[0] = 0;
    clear_snake_mappings();
    return;
  }
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
  if (me && me->vlither_tag_id < 0 && us->vlither_tag_id >= 0)
    me->vlither_tag_id = us->vlither_tag_id;
}

void vlither_tags_destroy(tenv *env) {
  if (!S.ready) return;
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
#ifdef ANDROID
  if (igButton("Join Discord", (ImVec2){0, 0}))
    android_jni_open_url("https://discord.gg/CJEeSScTJs");
#endif
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
