#ifdef ANDROID
#include "../android_glfw_shim.h"
#include <android/log.h>
#define DLOG(fmt,...) do{char _b[256];snprintf(_b,sizeof(_b),fmt,##__VA_ARGS__);    __android_log_print(ANDROID_LOG_ERROR,"vlither","%s",_b);}while(0)
#else
#define DLOG(fmt,...) do{}while(0)
#endif
#include "loop.h"

#include "../network/server.h"
#include "../user.h"
#include "input.h"
#include "oef.h"
#include "redraw.h"
#include "ui_overlay.h"

void game_loop(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  if (!env->config.running) gdata->conn = DISCONNECTED;

  switch (gdata->conn) {
    case CONNECTING: {
      usr->r->global.bg_opacity = 0;
      usr->r->global.bd_opacity = 0;
      usr->r->global.minimap_opacity = 0;

      double connect_elapsed = glfwGetTime() - gdata->connect_started_at;
      if (gdata->connection && connect_elapsed > TIMEOUT) {
        gdata->connection->is_closing = true;
        DLOG("TIMEOUT: connect elapsed %.2f > %d", connect_elapsed, TIMEOUT);
        printf("Connection timed out.");
      }

      server_poll(env);

      vec2 loading_bar = {500, 12};
      igSetCursorPosX(ctx->size[0] * 0.5f - loading_bar[0] * 0.5f);
      igSetCursorPosY(ctx->size[1] * 0.5f - loading_bar[1] * 0.5f);

      igPushStyleColor_Vec4(ImGuiCol_PlotHistogram,
                            (ImVec4){0.168f, 0.668f, 0.375f, 1});
      igProgressBar(-glfwGetTime(), (ImVec2){loading_bar[0], loading_bar[1]},
                    NULL);
      igPopStyleColor(1);

      if (gdata->restart_req) {
        const char *reconnect_text = "Connection interrupted - reconnecting...";
        ImVec2 reconnect_size;
        igCalcTextSize(&reconnect_size, reconnect_text, NULL, false, -1.0f);
        igSetCursorPosX(ctx->size[0] * 0.5f - reconnect_size.x * 0.5f);
        igTextColored((ImVec4){1.0f, 0.78f, 0.28f, 1.0f}, "%s",
                      reconnect_text);
      }

      if (gdata->closed) {
        gdata->connection = NULL;
        gdata->closed = false;
        if (gdata->restart_req && !gdata->suppress_reconnect &&
            gdata->reconnect_attempts < 3) {
          gdata->reconnect_attempts++;
          game_data_reset(env);
          gdata->conn = CONNECTING;
          server_connect(env);
        } else {
          gdata->restart_req = false;
          gdata->reconnect_attempts = 0;
          gdata->conn = DISCONNECTED;
        }
      }
      break;
    }
    case CONNECTED:
      time_step(env);
      input(env);
      server_poll(env);
      oef(env);
      redraw(env);
      ui_overlay(env);

      if (!gdata->closed && gdata->connection &&
          (usrs->hotkeys[HOTKEY_QUIT].active ||
           (usrs->quit_mc &&
            tmouse_button_pressed(env->ms, GLFW_MOUSE_BUTTON_MIDDLE)))) {
        gdata->suppress_reconnect = true;
        gdata->restart_req = false;
        gdata->connection->is_closing = true;
      } else if (!gdata->closed && gdata->connection &&
                 (usrs->hotkeys[HOTKEY_RESTART].active ||
                  (usrs->restart_rc &&
                   tmouse_button_pressed(env->ms, GLFW_MOUSE_BUTTON_RIGHT)))) {
        gdata->suppress_reconnect = false;
        gdata->connection->is_closing = true;
        gdata->restart_req = true;
      }

      if (gdata->closed) {
        bool reconnect = gdata->restart_req && !gdata->suppress_reconnect;
        if (reconnect && !gdata->preview_active) {
          usrs->kills = gdata->data.kills;
          usrs->score = gdata->data.score;
          usrs->play_time = gdata->data.play_etm;
          save_user_settings(usrs);
        }
        game_data_reset(env);
        gdata->connection = NULL;

        if (reconnect) {
          if (gdata->reconnect_attempts < 1) gdata->reconnect_attempts = 1;
          usr->gdata.conn = CONNECTING;
          server_connect(env);
        } else {
          gdata->restart_req = false;
          gdata->reconnect_attempts = 0;
          usr->gdata.conn = DISCONNECTED;
        }
        gdata->closed = false;
      }

      break;
    case DISCONNECTED:
      usr->r->global.bg_opacity = 0;
      usr->r->global.bd_opacity = 0;
      usr->r->global.minimap_opacity = 0;

      gdata->curr_screen = TITLE_SCREEN;

      game_data_reset(env);
      server_poll(env);

      break;
  }
}
