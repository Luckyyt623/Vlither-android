#include "snakey_rain.h"

#include "../external/mongoose.h"
#include "../user.h"
#include "ntl_team.h"
#include "vlither_tags.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SR_HOST "snakeyrain.com"
#define SR_PORT 8421
#define SR_VERSION_MAJOR 6
#define SR_VERSION_MINOR 36
#define SR_SPAWN_MAX 512

typedef struct sr_writer {
  uint8_t data[768];
  size_t len;
} sr_writer;

typedef struct sr_state {
  bool initialized;
  bool enabled_at_start; /* runtime active flag (no restart needed) */
  bool mgr_ready;
  bool shutting_down;
  bool ws_open;
  bool logged_in;
  bool start_requested;
  bool bots_running;
  bool boost;
  int mode;
  int bot_count;
  int account_max_bots;
  int desired_max_bots;
  double last_position_send;
  struct mg_mgr mgr;
  struct mg_connection *connection;
  char username[64];
  char password[64];
  char bot_name[25];
  char bot_skin[128];
  char status[96];
  char direction[48];
  char message[160];
  uint8_t spawn_packet[SR_SPAWN_MAX];
  size_t spawn_packet_len;
} sr_state;

static sr_state S;

static void sr_local_chat(const char *text) {
  if (!text || !text[0]) return;
  /* Local-only notifications — never broadcast to other players. */
  ntl_team_system_message(text);
  vlither_chat_system_message(text);
}

static void sr_status(const char *text) {
  snprintf(S.status, sizeof S.status, "%s", text ? text : "");
  /* Only push bot spawn / stop style events into chat for the local user. */
  if (!text || !text[0]) return;
  if (strstr(text, "Bots running") || strstr(text, "Bots stopped") ||
      strstr(text, "Starting bots") || strstr(text, "Stopping bots") ||
      strstr(text, "Waiting for your snake to spawn")) {
    sr_local_chat(text);
  }
}

static bool sr_put_u8(sr_writer *w, unsigned value) {
  if (!w || w->len + 1 > sizeof w->data) return false;
  w->data[w->len++] = (uint8_t)value;
  return true;
}

static bool sr_put_u16(sr_writer *w, unsigned value) {
  if (!w || w->len + 2 > sizeof w->data) return false;
  w->data[w->len++] = (uint8_t)(value & 255u);
  w->data[w->len++] = (uint8_t)((value >> 8) & 255u);
  return true;
}

static bool sr_put_string8(sr_writer *w, const char *text) {
  size_t len = text ? strlen(text) : 0;
  if (len > 255) len = 255;
  if (!sr_put_u8(w, (unsigned)len) || w->len + len > sizeof w->data)
    return false;
  if (len) memcpy(w->data + w->len, text, len);
  w->len += len;
  return true;
}

static bool sr_put_string16(sr_writer *w, const char *text) {
  size_t len = text ? strlen(text) : 0;
  if (len > 65535) len = 65535;
  if (!sr_put_u16(w, (unsigned)len) || w->len + len > sizeof w->data)
    return false;
  if (len) memcpy(w->data + w->len, text, len);
  w->len += len;
  return true;
}

static unsigned sr_read_u16(const uint8_t *data, size_t len, size_t offset) {
  if (!data || offset + 2 > len) return 0;
  return (unsigned)data[offset] | ((unsigned)data[offset + 1] << 8);
}

static void sr_send_raw(const void *data, size_t len, bool before_login) {
  if (!S.connection || !S.ws_open || !data || !len) return;
  if (!before_login && !S.logged_in) return;
  mg_ws_send(S.connection, data, len, WEBSOCKET_OP_BINARY);
}

static void sr_send_writer(const sr_writer *w, bool before_login) {
  if (w && w->len) sr_send_raw(w->data, w->len, before_login);
}

static void sr_send_login(void) {
  sr_writer w = {0};
  sr_put_u8(&w, 'a');
  sr_put_string16(&w, S.username);
  sr_put_string16(&w, S.password);
  sr_put_u8(&w, SR_VERSION_MAJOR);
  sr_put_u8(&w, SR_VERSION_MINOR);
  sr_send_writer(&w, true);
}

static void sr_send_character(void) {
  const char *name = S.bot_name[0] ? S.bot_name : "SnakeyRain";
  const char *skin = S.bot_skin[0] ? S.bot_skin :
      "uuuuuuuauuuuuuaauuuuuaaauuuuaaaauuuaaaaauuaaaaaauaaaaaaa"
      "uuaaaaaauuuaaaaauuuuaaaauuuuuaaauuuuuuaa";
  sr_writer w = {0};
  sr_put_u8(&w, 'c');
  sr_put_u8(&w, SR_VERSION_MAJOR);
  sr_put_u8(&w, SR_VERSION_MINOR);
  sr_put_u8(&w, 1);
  sr_put_string8(&w, name);
  sr_put_string8(&w, skin);
  sr_put_string8(&w, "12");
  sr_put_u8(&w, 11);
  sr_put_string8(&w, "");
  sr_send_writer(&w, false);
}

static void sr_send_tuning(void) {
  sr_writer w = {0};
  sr_put_u8(&w, 'T');
  sr_put_u16(&w, 2000);
  sr_put_u16(&w, 800);
  sr_put_u8(&w, 90);
  sr_put_u16(&w, 50);
  sr_put_u8(&w, 1);
  sr_put_u16(&w, 2000);
  sr_put_u8(&w, 45);
  sr_put_u16(&w, 1000);
  sr_send_writer(&w, false);

  sr_writer evolve = {0};
  sr_put_u8(&evolve, 'E');
  sr_put_u8(&evolve, 0);
  sr_put_u16(&evolve, 30);
  sr_put_u8(&evolve, 1);
  sr_send_writer(&evolve, false);
}

static void sr_send_max_bots(void) {
  int limit = S.desired_max_bots;
  if (S.account_max_bots > 0 && limit > S.account_max_bots)
    limit = S.account_max_bots;
  if (limit < 1) limit = 1;
  sr_writer w = {0};
  sr_put_u8(&w, 'B');
  sr_put_u16(&w, (unsigned)limit);
  sr_send_writer(&w, false);
}

static void sr_send_spawn(void) {
  if (!S.spawn_packet_len) return;
  sr_send_raw(S.spawn_packet, S.spawn_packet_len, false);
}

static bool sr_current_position(tenv *env, uint16_t *x, uint16_t *y) {
  if (!env || !env->usr || !x || !y) return false;
  game_data *g = &env->usr->gdata;
  snake *me = get_snake(g, g->data.snake_id);
  if (!me || !isfinite(me->xx) || !isfinite(me->yy)) return false;
  float fx = fmaxf(0.0f, fminf(me->xx + me->fx, 65535.0f));
  float fy = fmaxf(0.0f, fminf(me->yy + me->fy, 65535.0f));
  *x = (uint16_t)lroundf(fx);
  *y = (uint16_t)lroundf(fy);
  return true;
}

static void sr_send_origin(tenv *env) {
  uint16_t x, y;
  if (!sr_current_position(env, &x, &y)) return;
  sr_writer w = {0};
  sr_put_u8(&w, 'o');
  sr_put_u16(&w, x);
  sr_put_u16(&w, y);
  sr_send_writer(&w, false);
}

static void sr_send_mode(tenv *env, int mode) {
  if (mode < 1 || mode > 14 || mode == 12) return;
  S.mode = mode;
  if (mode == 8 || mode == 9 || mode == 14) sr_send_origin(env);
  uint8_t command[2] = {'d', (uint8_t)mode};
  sr_send_raw(command, sizeof command, false);
}

static void sr_send_server(tenv *env) {
  if (!env || !env->usr || !S.logged_in || !S.spawn_packet_len) return;
  const char *address = env->usr->usrs.server_address;
  const char *colon = strrchr(address, ':');
  if (!colon || colon == address || !colon[1] ||
      strchr(address, ':') != colon) {
    sr_status("Snakey Rain requires an IPv4 game server");
    return;
  }
  char host[128] = {0};
  char port[16] = {0};
  size_t host_len = (size_t)(colon - address);
  if (host_len >= sizeof host) host_len = sizeof host - 1;
  memcpy(host, address, host_len);
  snprintf(port, sizeof port, "%s", colon + 1);

  sr_writer w = {0};
  sr_put_u8(&w, 'r');
  sr_put_string8(&w, host);
  sr_put_string8(&w, port);
  sr_send_writer(&w, false);
  sr_status("Starting bots...");
}

static void sr_try_start(tenv *env) {
  if (!S.start_requested) return;
  if (!S.logged_in) {
    sr_status("Connecting to Snakey Rain...");
    return;
  }
  if (!S.spawn_packet_len) {
    sr_status("Waiting for your snake to spawn...");
    return;
  }
  sr_send_server(env);
}

static void sr_after_login(tenv *env) {
  sr_send_character();
  sr_send_tuning();
  sr_send_max_bots();
  sr_send_spawn();
  sr_send_mode(env, S.mode);
  sr_try_start(env);
}

static void sr_read_message(const uint8_t *data, size_t len) {
  if (!data || len < 3) return;
  size_t text_len = sr_read_u16(data, len, 1);
  if (text_len > len - 3) text_len = len - 3;
  if (text_len >= sizeof S.message) text_len = sizeof S.message - 1;
  memcpy(S.message, data + 3, text_len);
  S.message[text_len] = 0;
}

static void sr_callback(struct mg_connection *c, int ev, void *ev_data) {
  tenv *env = (tenv *)c->fn_data;
  if (ev == MG_EV_CONNECT) {
    if (c->is_tls) {
      struct mg_tls_opts opts = {.skip_verification = 1};
      mg_tls_init(c, &opts);
    }
  } else if (ev == MG_EV_WS_OPEN) {
    S.ws_open = true;
    sr_status("Logging in...");
    sr_send_login();
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
    const uint8_t *data = (const uint8_t *)msg->data.buf;
    size_t len = msg->data.len;
    if (!data || len < 1) return;
    switch (data[0]) {
      case 'l':
        if (len < 2 || !data[1]) {
          sr_status("Snakey Rain login failed");
          c->is_closing = true;
          return;
        }
        S.logged_in = true;
        S.account_max_bots = len >= 4 ? (int)sr_read_u16(data, len, 2) : 0;
        sr_status("Logged in");
        sr_after_login(env);
        break;
      case 'g':
        if (len >= 2) S.mode = data[1];
        break;
      case 'b':
        if (len >= 2) S.boost = !!data[1];
        break;
      case 'i':
        if (len >= 3) S.bot_count = (int)sr_read_u16(data, len, 1);
        break;
      case 'w':
        if (len >= 2) {
          S.bots_running = !!data[1];
          sr_status(S.bots_running ? "Bots running" : "Bots stopped");
        }
        break;
      case 'm':
        sr_read_message(data, len);
        break;
      case 'd':
        if (len >= 2) {
          size_t n = data[1];
          if (n > len - 2) n = len - 2;
          if (n >= sizeof S.direction) n = sizeof S.direction - 1;
          memcpy(S.direction, data + 2, n);
          S.direction[n] = 0;
        }
        break;
      case 'P': {
        uint8_t pong[2] = {'P', 1};
        sr_send_raw(pong, sizeof pong, false);
        break;
      }
    }
  } else if (ev == MG_EV_ERROR) {
    sr_status("Snakey Rain connection error");
    c->is_closing = true;
  } else if (ev == MG_EV_CLOSE) {
    S.connection = NULL;
    S.ws_open = false;
    S.logged_in = false;
    S.bots_running = false;
    S.bot_count = 0;
    if (!S.shutting_down)
      sr_status(S.start_requested ? "Disconnected - tap Start to retry"
                                  : "Logged out");
  }
}

static void sr_connect(tenv *env) {
  if (!S.enabled_at_start || S.connection) return;
  char url[96];
  snprintf(url, sizeof url, "wss://%s:%d/", SR_HOST, SR_PORT);
  S.connection = mg_ws_connect(&S.mgr, url, sr_callback, env,
                               "Origin: https://slither.io\r\n");
  if (S.connection)
    sr_status("Connecting to Snakey Rain...");
  else
    sr_status("Unable to create Snakey Rain connection");
}

static void sr_copy_settings(user_settings *us) {
  if (!us) return;
  snprintf(S.username, sizeof S.username, "%s", us->snakey_rain_username);
  snprintf(S.password, sizeof S.password, "%s", us->snakey_rain_password);
  snprintf(S.bot_name, sizeof S.bot_name, "%s", us->snakey_rain_bot_name);
  snprintf(S.bot_skin, sizeof S.bot_skin, "%s", us->snakey_rain_bot_skin);
  S.desired_max_bots = us->snakey_rain_max_bots;
  if (!S.bot_name[0]) snprintf(S.bot_name, sizeof S.bot_name, "SnakeyRain");
  if (!S.bot_skin[0])
    snprintf(S.bot_skin, sizeof S.bot_skin,
             "uuuuuuuauuuuuuaauuuuuaaauuuuaaaauuuaaaaauuaaaaaauaaaaaaa"
             "uuaaaaaauuuaaaaauuuuaaaauuuuuaaauuuuuuaa");
  if (S.desired_max_bots < 1) S.desired_max_bots = 1;
}

static void sr_enable(tenv *env) {
  if (!env || !env->usr) return;
  sr_copy_settings(&env->usr->usrs);
  if (!S.mgr_ready) {
    mg_mgr_init(&S.mgr);
    S.mgr_ready = true;
  }
  S.enabled_at_start = true;
  sr_status("Ready");
}

static void sr_disable(void) {
  S.start_requested = false;
  S.bots_running = false;
  S.logged_in = false;
  S.ws_open = false;
  S.bot_count = 0;
  S.spawn_packet_len = 0;
  if (S.connection) {
    S.connection->is_closing = 1;
    S.connection = NULL;
  }
  S.enabled_at_start = false;
  sr_status("Disabled");
}

void snakey_rain_init(tenv *env) {
  memset(&S, 0, sizeof S);
  S.initialized = true;
  S.mode = 1;
  snprintf(S.direction, sizeof S.direction, "Follow");
  if (env && env->usr && env->usr->usrs.snakey_rain_enabled)
    sr_enable(env);
  else
    sr_status("Disabled");
}

void snakey_rain_destroy(tenv *env) {
  (void)env;
  if (!S.initialized) return;
  S.shutting_down = true;
  if (S.connection) S.connection->is_closing = 1;
  if (S.mgr_ready) mg_mgr_free(&S.mgr);
  memset(&S, 0, sizeof S);
}

bool snakey_rain_enabled_at_start(void) { return S.enabled_at_start; }

bool snakey_rain_requires_restart(tenv *env) {
  (void)env;
  return false; /* settings apply live — no app restart */
}

void snakey_rain_apply_settings(tenv *env) {
  if (!S.initialized || !env || !env->usr) return;
  user_settings *us = &env->usr->usrs;
  if (!us->snakey_rain_enabled) {
    if (S.enabled_at_start) sr_disable();
    return;
  }
  if (!S.enabled_at_start) {
    sr_enable(env);
    return;
  }
  sr_copy_settings(us);
  if (S.logged_in) {
    sr_send_character();
    sr_send_max_bots();
    sr_status("Bot name/skin updated");
    sr_local_chat("Snakey Rain: bot name/skin applied");
  }
}

void snakey_rain_on_spawn(tenv *env, const uint8_t *packet,
                          size_t packet_len) {
  if (!S.enabled_at_start || !env || !env->usr || !packet || !packet_len ||
      env->usr->gdata.preview_active ||
      env->usr->gdata.curr_screen != PLAYING)
    return;
  if (packet_len + 1 > sizeof S.spawn_packet) return;
  S.spawn_packet[0] = 'S';
  memcpy(S.spawn_packet + 1, packet, packet_len);
  S.spawn_packet_len = packet_len + 1;
  if (S.logged_in) {
    sr_send_spawn();
    sr_try_start(env);
  }
}

void snakey_rain_update(tenv *env) {
  if (!S.initialized || !env || !env->usr) return;
  user_settings *us = &env->usr->usrs;
  /* Live toggle — no app restart required. */
  if (us->snakey_rain_enabled && !S.enabled_at_start)
    sr_enable(env);
  else if (!us->snakey_rain_enabled && S.enabled_at_start)
    sr_disable();
  if (!S.enabled_at_start) return;
  /* Keep credentials/name/skin in sync while idle. */
  if (!S.logged_in) sr_copy_settings(us);
  else if (S.desired_max_bots != us->snakey_rain_max_bots) {
    S.desired_max_bots = us->snakey_rain_max_bots;
    if (S.desired_max_bots < 1) S.desired_max_bots = 1;
    sr_send_max_bots();
  }
  mg_mgr_poll(&S.mgr, 0);
  if (!S.logged_in || !S.bots_running || !env || !env->usr ||
      env->usr->gdata.curr_screen != PLAYING ||
      env->usr->gdata.conn != CONNECTED)
    return;
  if (S.mode != 1 && S.mode != 6 && S.mode != 9) return;
  double now = igGetTime();
  if (now - S.last_position_send < 0.03) return;
  uint16_t x, y;
  if (!sr_current_position(env, &x, &y)) return;
  sr_writer w = {0};
  sr_put_u8(&w, 'p');
  sr_put_u16(&w, x);
  sr_put_u16(&w, y);
  sr_send_writer(&w, false);
  S.last_position_send = now;
}

static const char *sr_mode_name(int mode) {
  switch (mode) {
    case 1: return "Follow";
    case 2: return "Unfollow";
    case 3: return "Random";
    case 4: return "Straight";
    case 5: return "Copy";
    case 8: return "Rain";
    case 9: return "Tornado";
    case 10: return "Circle CW";
    case 11: return "Circle CCW";
    case 13: return "Heart";
    case 14: return "Wigwag";
    default: return "Follow";
  }
}

static void sr_toggle_start(tenv *env) {
  if (S.bots_running || S.start_requested) {
    uint8_t stop[2] = {'x', 0};
    sr_send_raw(stop, sizeof stop, false);
    S.start_requested = false;
    sr_status("Stopping bots...");
    return;
  }
  S.start_requested = true;
  if (!S.connection) sr_connect(env);
  sr_try_start(env);
}

static void sr_toggle_boost(void) {
  S.boost = !S.boost;
  uint8_t boost[2] = {'b', S.boost ? 1 : 0};
  sr_send_raw(boost, sizeof boost, false);
}

void snakey_rain_draw(tenv *env) {
  if (!S.enabled_at_start || !env || !env->usr || !env->ctx ||
      env->usr->gdata.curr_screen != PLAYING)
    return;

  float scale = fmaxf(0.85f, fminf((float)env->ctx->size[1] / 720.0f, 1.25f));
  float pad = 6.0f * scale;
  float btn_h = 30.0f * scale;
  float gap = 8.0f * scale;

  /* Measure compact pill widths */
  char bots_label[32];
  snprintf(bots_label, sizeof bots_label, "Bots=%d", S.bot_count);
  const char *follow_label = sr_mode_name(S.mode);
  const char *boost_label = S.boost ? "Boost" : "No Boost";

  ImVec2 bots_sz, follow_sz, boost_sz;
  igCalcTextSize(&bots_sz, bots_label, NULL, false, -1.0f);
  igCalcTextSize(&follow_sz, follow_label, NULL, false, -1.0f);
  igCalcTextSize(&boost_sz, boost_label, NULL, false, -1.0f);

  float bots_w = bots_sz.x + pad * 2.5f;
  float follow_w = follow_sz.x + pad * 2.5f;
  float boost_w = boost_sz.x + pad * 2.5f;
  float width = bots_w + follow_w + boost_w + gap * 2.0f + pad * 2.0f;
  float height = btn_h + pad * 2.0f;
  float x = ((float)env->ctx->size[0] - width) * 0.5f;
  float y = 8.0f * scale;

#ifdef ANDROID
  android_ui_capture_rect(x, y, x + width, y + height);
#endif

  igSetNextWindowPos((ImVec2){x, y}, ImGuiCond_Always, (ImVec2){});
  igSetNextWindowSize((ImVec2){width, height}, ImGuiCond_Always);
  igSetNextWindowBgAlpha(0.0f);
  bool open = igBegin("##snakey_rain_bar", NULL,
                      ImGuiWindowFlags_NoTitleBar |
                          ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoBackground);
  if (open) {
    /* Bots pill — tap starts/stops bots */
    ImVec4 bots_col = S.bots_running
                          ? (ImVec4){0.35f, 0.95f, 0.55f, 1.0f}
                          : (ImVec4){1.0f, 0.55f, 0.75f, 1.0f};
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.12f, 0.12f, 0.16f, 0.92f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.20f, 0.20f, 0.28f, 0.95f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.28f, 0.28f, 0.36f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_Text, bots_col);
    igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 14.0f * scale);
    if (igButton(bots_label, (ImVec2){bots_w, btn_h}))
      sr_toggle_start(env);
    igPopStyleColor(1);

    igSameLine(0, gap);

    /* Follow / mode pill — cycles through modes when tapped */
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.95f, 0.95f, 1.0f, 1.0f});
    if (igButton(follow_label, (ImVec2){follow_w, btn_h})) {
      static const int modes[] = {1, 2, 3, 4, 5, 8, 9, 10, 11, 13, 14};
      int next = modes[0];
      for (size_t i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        if (modes[i] == S.mode) {
          next = modes[(i + 1) % (sizeof modes / sizeof modes[0])];
          break;
        }
      }
      sr_send_mode(env, next);
    }
    igPopStyleColor(1);

    igSameLine(0, gap);

    /* Boost / No Boost pill */
    ImVec4 boost_col = S.boost ? (ImVec4){1.0f, 0.85f, 0.25f, 1.0f}
                               : (ImVec4){0.85f, 0.85f, 0.90f, 1.0f};
    igPushStyleColor_Vec4(ImGuiCol_Text, boost_col);
    if (igButton(boost_label, (ImVec2){boost_w, btn_h}))
      sr_toggle_boost();
    igPopStyleColor(1);

    igPopStyleVar(1);
    igPopStyleColor(3);
  }
  igEnd();
}
