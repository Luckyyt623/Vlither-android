#include "bg_preview.h"

#ifdef ANDROID
#include "../android_glfw_shim.h"
#include <android/log.h>
#define DLOG(fmt, ...)                                                    \
  do {                                                                    \
    char _b[256];                                                         \
    snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__);                         \
    __android_log_print(ANDROID_LOG_DEBUG, "vlither_bgprev", "%s", _b);   \
  } while (0)
#else
#define DLOG(fmt, ...) (void)0
#endif

#include <math.h>

#include "../network/server.h"
#include "../user.h"
#include "oef.h"
#include "redraw.h"
#include "ui_overlay.h"

/* Internal state. Kept static/module-private on purpose: nothing else
   should reach into this, all interaction goes through bg_preview_*(). */
static bool s_owns     = false;  /* do we currently believe we own a preview session */
static bool s_closing  = false;  /* asked our connection to close, waiting for confirmation */
static bool s_saved_bot_hotkey = false;
static struct mg_connection* s_conn = NULL; /* OUR connection, captured right after connect */

/* Local elapsed-time bookkeeping for the CURRENT connection attempt.
   IMPORTANT: this deliberately never calls glfwSetTime(0). That call
   resets the engine's single global clock, which every other system
   (physics delta-time in oef.c, animations, fade transitions, real
   play's own connection timeout, etc.) also reads. The original
   game_loop()/title_screen.c only ever call it once, for a user-driven
   Play click. This module can reconnect many times on its own (every
   time the hidden preview snake dies), and doing that against the
   *global* clock was corrupting delta-time everywhere else the longer
   a Settings/Controls session stayed open — the likely cause of the
   instability reported after leaving Settings/Controls open for a
   while. Track our own reference point instead. */
static double s_session_started_at = 0.0;

/* Basic reconnect backoff so a run of quick deaths/failures (much more
   likely on a busy official server than a quiet test one) can't turn
   into a tight reconnect loop hammering the server and the local
   networking stack. Grows on quick failures, resets after a healthy
   run. */
static double s_backoff = 1.0;
static double s_next_attempt_at = 0.0;

static bool cooldown_elapsed(void) { return glfwGetTime() >= s_next_attempt_at; }

static void note_session_ended(void) {
  double duration = glfwGetTime() - s_session_started_at;
  if (duration < 3.0) {
    s_backoff = s_backoff * 2.0;
    if (s_backoff > 10.0) s_backoff = 10.0;
  } else {
    s_backoff = 1.0;
  }
  s_next_attempt_at = glfwGetTime() + s_backoff;
}

static bool bg_preview_wanted(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;

  return gdata->curr_screen == SETTINGS || gdata->curr_screen == CONTROLS;
}

/* Minimal stand-in for game/input.c's input(), used only while the
   preview owns the connection. Deliberately does NOT read real mouse,
   keyboard, or hotkey state — it steers purely from the bot's own
   output so that clicking around in Settings/Controls (or typing a
   nickname) can never leak into the hidden snake's movement or
   corrupt the player's real hotkey toggle states. */
static void bg_preview_bot_input(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  struct mg_connection* connection = gdata->connection;
  if (!connection) return;

  if (!gdata->data.wfpr) {
    if (gdata->data.ctm - gdata->data.last_ping_mtm > 250) {
      gdata->data.last_ping_mtm = gdata->data.ctm;
      gdata->data.wfpr = true;
      mg_ws_send(connection, (uint8_t[]){251}, 1, WEBSOCKET_OP_BINARY);
    }
  }

  if (!gdata->data.follow_view) return;

  int snakes_len = tdarray_length(gdata->data.snakes);
  if (snakes_len <= 0) return;
  snake* me = gdata->data.snakes + (snakes_len - 1);

  int xm = (int)gdata->bot.output.xm;
  int ym = (int)gdata->bot.output.ym;

  gdata->data.wmd = gdata->bot.output.accel;
  if (gdata->data.md != gdata->data.wmd &&
      gdata->data.ctm - gdata->data.last_accel_mtm > 150) {
    gdata->data.md = gdata->data.wmd;
    gdata->data.last_accel_mtm = gdata->data.ctm;
    mg_ws_send(connection, (uint8_t[]){gdata->data.md ? 253 : 254}, 1,
               WEBSOCKET_OP_BINARY);
  }

  bool want_e = false;
  if (xm != gdata->data.lsxm || ym != gdata->data.lsym) want_e = true;
  me->eang = atan2f((float)ym, (float)xm);
  if (want_e && gdata->data.ctm - gdata->data.last_e_mtm > 50) {
    gdata->data.last_e_mtm = gdata->data.ctm;
    gdata->data.lsxm = xm;
    gdata->data.lsym = ym;
    float d2 = (float)xm * (float)xm + (float)ym * (float)ym;
    float ang;
    if (d2 > 256) {
      ang = atan2f((float)ym, (float)xm);
      me->eang = ang;
    } else {
      ang = me->wang;
    }
    ang = fmodf(ang, PI2);
    if (ang < 0) ang += PI2;
    int sang = (int)floorf((250 + 1) * ang / PI2);
    if (sang != gdata->data.lsang) {
      gdata->data.lsang = sang;
      mg_ws_send(connection, (uint8_t[]){sang & 255}, 1, WEBSOCKET_OP_BINARY);
    }
  }
}

static void bg_preview_begin(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  s_saved_bot_hotkey = usrs->hotkeys[HOTKEY_BOT].active;
  usrs->hotkeys[HOTKEY_BOT].active = true;

  gdata->preview_active = true;
  gdata->conn = CONNECTING;
  s_session_started_at = glfwGetTime();
  server_connect(env);

  s_conn    = gdata->connection;
  s_owns    = true;
  s_closing = false;

  /* First-ever connect for this session shouldn't be held back by
     backoff from some earlier, now-irrelevant run. */
  s_backoff = 1.0;
  s_next_attempt_at = 0.0;
}

static void bg_preview_finish(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  usrs->hotkeys[HOTKEY_BOT].active = s_saved_bot_hotkey;
  gdata->preview_active = false;
  s_owns    = false;
  s_closing = false;
  s_conn    = NULL;
}

bool bg_preview_visible(tenv* env) {
  (void)env;
  return s_owns;
}

void bg_preview_update(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;

  if (!s_owns) {
    if (!bg_preview_wanted(env)) return;
    /* Never hijack a real, already-active session. Settings/Controls are
       normally only reachable from the title screen while disconnected,
       but this costs nothing and keeps us honest. */
    if (gdata->conn != DISCONNECTED) return;

    bg_preview_begin(env);
    return;
  }

  /* We believe we own a preview session. First make sure a real "Play"
     click hasn't quietly taken over the shared connection out from
     under us. If it has, close OUR OWN connection object directly (not
     gdata->connection, which now belongs to the real session) and step
     aside without touching anything the real session owns. */
  if (gdata->connection != s_conn) {
    if (s_conn) s_conn->is_closing = true;
    bg_preview_finish(env);
    return;
  }

  bool want = bg_preview_wanted(env);

  if (want && !s_closing) {
    switch (gdata->conn) {
      case CONNECTING:
        if (glfwGetTime() - s_session_started_at > TIMEOUT)
          gdata->connection->is_closing = true;
        server_poll(env);
        if (gdata->closed) {
          game_data_reset(env);
          gdata->closed = false;
          gdata->conn = DISCONNECTED;
          note_session_ended();
        }
        break;

      case CONNECTED:
        time_step(env);
        bg_preview_bot_input(env);
        server_poll(env);
        oef(env);
        redraw(env);
        /* No full HUD here — but on the Controls screen specifically,
           draw the real overlay (boost button, joystick, zoom slider,
           on-screen hotkey buttons) so the player can actually see
           where each one lands as they adjust its position sliders. */
        if (gdata->curr_screen == CONTROLS) ui_overlay(env);

        if (gdata->closed) {
          game_data_reset(env);
          gdata->closed = false;
          gdata->conn = DISCONNECTED;
          note_session_ended();
        }
        break;

      case DISCONNECTED:
        /* Respawn, but not faster than the backoff allows — avoids a
           tight reconnect loop if the server keeps closing us quickly. */
        if (!cooldown_elapsed()) break;
        game_data_reset(env);
        gdata->conn = CONNECTING;
        s_session_started_at = glfwGetTime();
        server_connect(env);
        s_conn = gdata->connection;
        break;
    }
    return;
  }

  /* The screen changed (or we're already mid-teardown): ask the
     connection to close and keep polling until it confirms, without
     spawning any new snake in the meantime. */
  s_closing = true;
  if (gdata->conn != DISCONNECTED && gdata->connection) {
    gdata->connection->is_closing = true;
  }
  server_poll(env);
  if (gdata->closed) {
    game_data_reset(env);
    gdata->closed = false;
    gdata->conn = DISCONNECTED;
  }
  if (gdata->conn == DISCONNECTED) {
    bg_preview_finish(env);
  }
}
