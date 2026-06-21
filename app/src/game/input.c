#include <string.h>
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif
#include "input.h"

#include "../user.h"

void input(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  struct mg_connection* connection = gdata->connection;

  if (!gdata->data.wfpr) {
    if (gdata->data.ctm - gdata->data.last_ping_mtm > 250) {
      gdata->data.last_ping_mtm = gdata->data.ctm;
      gdata->data.wfpr = true;
      mg_ws_send(connection, (uint8_t[]){251}, 1, WEBSOCKET_OP_BINARY);
    }
  }

  if (gdata->data.follow_view) {
    int xm;
    int ym;

    int snakes_len = tdarray_length(gdata->data.snakes);
    snake* me = gdata->data.snakes + (snakes_len - 1);

    if (usrs->hotkeys[HOTKEY_BOT].active) {
      xm = gdata->bot.output.xm;
      ym = gdata->bot.output.ym;
    } else {
      if (twindow_key_down(env->wnd, GLFW_KEY_LEFT))
        gdata->data.kd_l_frb += gdata->data.vfrb;
      if (twindow_key_down(env->wnd, GLFW_KEY_RIGHT))
        gdata->data.kd_r_frb += gdata->data.vfrb;

      if (gdata->data.kd_l_frb > 0 || gdata->data.kd_r_frb > 0)
        if (gdata->data.ctm - gdata->data.lkstm > 150) {
          gdata->data.lkstm = gdata->data.ctm;
          if (gdata->data.kd_r_frb > 0)
            if (gdata->data.kd_l_frb > gdata->data.kd_r_frb) {
              gdata->data.kd_l_frb -= gdata->data.kd_r_frb;
              gdata->data.kd_r_frb = 0;
            }
          if (gdata->data.kd_l_frb > 0)
            if (gdata->data.kd_r_frb > gdata->data.kd_l_frb) {
              gdata->data.kd_r_frb -= gdata->data.kd_l_frb;
              gdata->data.kd_l_frb = 0;
            }
          if (gdata->data.kd_l_frb > 0) {
            int v = gdata->data.kd_l_frb;
            if (v > 127) v = 127;
            gdata->data.kd_l_frb -= v;
            me->eang -= gdata->data.mamu * v * me->scang * me->spang;
            mg_ws_send(connection, (uint8_t[]){252, (uint8_t)v}, 2,
                       WEBSOCKET_OP_BINARY);
          } else if (gdata->data.kd_r_frb > 0) {
            int v = gdata->data.kd_r_frb;
            if (v > 127) v = 127;
            gdata->data.kd_r_frb -= v;
            me->eang += gdata->data.mamu * v * me->scang * me->spang;
            v += 128;
            mg_ws_send(connection, (uint8_t[]){252, (uint8_t)v}, 2,
                       WEBSOCKET_OP_BINARY);
          }
        }

#ifdef ANDROID
      /* env->ms->pos is always (0,0) on Android because tmouse_create does not
       * set ms->window – use wnd->touch.x/y directly instead.             */
      float tx = env->wnd->touch.x;
      float ty = env->wnd->touch.y;

      if (usrs->ctrl_mode_trackpad) {
        /* ============================================================
         * NTL TRACKPAD MODE
         * touch.down = movement finger (never the boost finger)
         * touch.boost_down = independent boost finger
         * ============================================================ */
        #define NTL_FORBIDDEN_R  25.0f   /* dead-zone radius at centre (JS: 25)  */
        #define NTL_SPAWN_R      50.0f   /* spawn offset from centre  (JS: 50)   */
        #define NTL_CURSOR_SPEED  1.8f   /* delta multiplier          (JS: 1.8)  */
        #define NTL_VEL_DECAY    0.85f
        #define NTL_VEL_WEIGHT   0.15f

        float sw = (float)ctx->size[0];
        float sh = (float)ctx->size[1];
        float cx = sw * 0.5f;
        float cy = sh * 0.5f;

        /* Movement touch – boost_down is now independent so no exclusion needed */
        bool touch_down = env->wnd->touch.down;

        if (touch_down) {
          if (env->wnd->touch.just_down || !gdata->touch_ctrl.tp_tracking) {
            /* ---- Touch start ---- */
            gdata->touch_ctrl.tp_tracking     = true;
            gdata->touch_ctrl.tp_last_touch_x = tx;
            gdata->touch_ctrl.tp_last_touch_y = ty;

            /* Spawn cursor at spawnRadius from last disappear angle */
            float ang = gdata->touch_ctrl.tp_disappear_angle;
            gdata->touch_ctrl.tp_cursor_x      = cx + NTL_SPAWN_R * cosf(ang);
            gdata->touch_ctrl.tp_cursor_y      = cy + NTL_SPAWN_R * sinf(ang);
            gdata->touch_ctrl.tp_prev_cursor_x = gdata->touch_ctrl.tp_cursor_x;
            gdata->touch_ctrl.tp_prev_cursor_y = gdata->touch_ctrl.tp_cursor_y;
            gdata->touch_ctrl.tp_vx              = 0.0f;
            gdata->touch_ctrl.tp_vy              = 0.0f;
            gdata->touch_ctrl.tp_visible         = true;
            gdata->touch_ctrl.tp_direction_found = false; /* JS: directionAngle = null */
          } else {
            /* ---- Touch move ---- */
            float dx = tx - gdata->touch_ctrl.tp_last_touch_x;
            float dy = ty - gdata->touch_ctrl.tp_last_touch_y;
            gdata->touch_ctrl.tp_last_touch_x = tx;
            gdata->touch_ctrl.tp_last_touch_y = ty;

            /* JS: on first movement >2px, re-spawn cursor at the actual swipe
             * direction angle (mirrors JS resetCursorWithDirection(directionAngle)) */
            if (!gdata->touch_ctrl.tp_direction_found &&
                (fabsf(dx) > 2.0f || fabsf(dy) > 2.0f)) {
              float dir_ang = atan2f(dy, dx);
              gdata->touch_ctrl.tp_cursor_x      = cx + NTL_SPAWN_R * cosf(dir_ang);
              gdata->touch_ctrl.tp_cursor_y      = cy + NTL_SPAWN_R * sinf(dir_ang);
              gdata->touch_ctrl.tp_prev_cursor_x = gdata->touch_ctrl.tp_cursor_x;
              gdata->touch_ctrl.tp_prev_cursor_y = gdata->touch_ctrl.tp_cursor_y;
              gdata->touch_ctrl.tp_direction_found = true;
            }

            /* Move cursor by delta * NTL_CURSOR_SPEED (JS: 1.8x) */
            float nx = gdata->touch_ctrl.tp_cursor_x + dx * NTL_CURSOR_SPEED;
            float ny = gdata->touch_ctrl.tp_cursor_y + dy * NTL_CURSOR_SPEED;

            /* Clamp to screen */
            nx = GLM_MAX(0.0f, GLM_MIN(sw, nx));
            ny = GLM_MAX(0.0f, GLM_MIN(sh, ny));

            /* Keep outside forbidden centre zone */
            float fdx  = nx - cx;
            float fdy  = ny - cy;
            float dist = sqrtf(fdx * fdx + fdy * fdy);
            if (dist < NTL_FORBIDDEN_R && dist > 0.001f) {
              float a = atan2f(fdy, fdx);
              nx = cx + cosf(a) * NTL_FORBIDDEN_R;
              ny = cy + sinf(a) * NTL_FORBIDDEN_R;
            }

            /* Velocity smoothing (for arrow rotation) */
            float mdx = nx - gdata->touch_ctrl.tp_prev_cursor_x;
            float mdy = ny - gdata->touch_ctrl.tp_prev_cursor_y;
            gdata->touch_ctrl.tp_vx =
                gdata->touch_ctrl.tp_vx * NTL_VEL_DECAY + mdx * NTL_VEL_WEIGHT;
            gdata->touch_ctrl.tp_vy =
                gdata->touch_ctrl.tp_vy * NTL_VEL_DECAY + mdy * NTL_VEL_WEIGHT;

            gdata->touch_ctrl.tp_prev_cursor_x = gdata->touch_ctrl.tp_cursor_x;
            gdata->touch_ctrl.tp_prev_cursor_y = gdata->touch_ctrl.tp_cursor_y;
            gdata->touch_ctrl.tp_cursor_x      = nx;
            gdata->touch_ctrl.tp_cursor_y      = ny;
          }

          /* Arrow angle = direction from cursor toward screen centre */
          gdata->touch_ctrl.tp_cursor_angle_deg =
              atan2f(cy - gdata->touch_ctrl.tp_cursor_y,
                     cx  - gdata->touch_ctrl.tp_cursor_x) *
              (180.0f / PI);

          /* Use cursor position as direction vector for snake */
          xm = (int)(gdata->touch_ctrl.tp_cursor_x - cx);
          ym = (int)(gdata->touch_ctrl.tp_cursor_y - cy);

        } else {
          /* ---- Touch lift ---- */
          if (gdata->touch_ctrl.tp_tracking) {
            gdata->touch_ctrl.tp_disappear_angle =
                atan2f(gdata->touch_ctrl.tp_cursor_y - cy,
                       gdata->touch_ctrl.tp_cursor_x - cx);
            gdata->touch_ctrl.tp_tracking        = false;
            gdata->touch_ctrl.tp_visible         = false;
            gdata->touch_ctrl.tp_direction_found = false;
          }
          /* No active touch – keep last heading */
          xm = (int)(gdata->touch_ctrl.tp_cursor_x - cx);
          ym = (int)(gdata->touch_ctrl.tp_cursor_y - cy);
        }

      } else {
        /* ============================================================
         * JOYSTICK MODE
         * touch.down = joystick finger (independent of boost)
         * ============================================================ */
        if (env->wnd->touch.down) {
          /* Record anchor on the very first frame of a new touch */
          if (env->wnd->touch.just_down || !gdata->touch_ctrl.joy_tracking) {
            gdata->touch_ctrl.joy_anchor_x = tx;
            gdata->touch_ctrl.joy_anchor_y = ty;
            gdata->touch_ctrl.joy_tracking = true;
          }
          /* Direction = displacement from anchor, ×4 so ~25 px = full deflection */
          xm = (int)((tx - gdata->touch_ctrl.joy_anchor_x) * 4.0f);
          ym = (int)((ty - gdata->touch_ctrl.joy_anchor_y) * 4.0f);
        } else {
          if (!env->wnd->touch.down) gdata->touch_ctrl.joy_tracking = false;
          /* No joystick touch – keep snake heading toward last touch absolute pos */
          xm = (int)tx - ctx->size[0] / 2;
          ym = (int)ty - ctx->size[1] / 2;
        }
      } /* end ctrl_mode_trackpad / joystick branch */
#else
      xm = (int)env->ms->pos[0] - ctx->size[0] / 2;
      ym = (int)env->ms->pos[1] - ctx->size[1] / 2;
#endif
    }
#ifdef ANDROID
    /* Boost fires only when the finger is in the right 20 % of the screen */
    gdata->data.wmd = env->wnd->touch.boost_down || gdata->bot.output.accel;
#else
    gdata->data.wmd = twindow_button_down(env->wnd, GLFW_MOUSE_BUTTON_LEFT) ||
                      twindow_key_down(env->wnd, GLFW_KEY_SPACE) ||
                      twindow_key_down(env->wnd, GLFW_KEY_UP) ||
                      gdata->bot.output.accel;
#endif

    if (gdata->data.md != gdata->data.wmd &&
        gdata->data.ctm - gdata->data.last_accel_mtm > 150) {
      gdata->data.md = gdata->data.wmd;
      gdata->data.last_accel_mtm = gdata->data.ctm;
      mg_ws_send(connection, (uint8_t[]){gdata->data.md ? 253 : 254}, 1,
                 WEBSOCKET_OP_BINARY);
    }

    bool want_e = false;
    if (xm != gdata->data.lsxm || ym != gdata->data.lsym) want_e = true;
    me->eang = atan2f(ym, xm);
    float ang;
    if (want_e && gdata->data.ctm - gdata->data.last_e_mtm > 50) {
      want_e = false;
      gdata->data.last_e_mtm = gdata->data.ctm;
      gdata->data.lsxm = xm;
      gdata->data.lsym = ym;
      float d2 = xm * xm + ym * ym;
      if (d2 > 256) {
        ang = atan2f(ym, xm);
        me->eang = ang;
      } else
        ang = me->wang;
      ang = fmodf(ang, PI2);
      if (ang < 0) ang += PI2;
      int sang = (int)floorf((250 + 1) * ang / PI2);
      if (sang != gdata->data.lsang) {
        gdata->data.lsang = sang;
        mg_ws_send(connection, (uint8_t[]){sang & 255}, 1, WEBSOCKET_OP_BINARY);
      }
    }
  }

  gdata->data.ms_zoom *= expf(env->ms->dwheel * usrs->zoom_step);

  if (tkeyboard_key_pressed(env->kb, GLFW_KEY_N) ||
      (GLFW_KEY_N < 512 && gdata->data.fake_key_pressed[GLFW_KEY_N]))
    gdata->data.ms_zoom *= expf(1 * usrs->zoom_step);
  else if (tkeyboard_key_pressed(env->kb, GLFW_KEY_M) ||
           (GLFW_KEY_M < 512 && gdata->data.fake_key_pressed[GLFW_KEY_M]))
    gdata->data.ms_zoom *= expf(-1 * usrs->zoom_step);

  gdata->data.ms_zoom =
      GLM_MAX(MAX_ZOOM_OUT, GLM_MIN(gdata->data.ms_zoom, MAX_ZOOM_IN));

  // hotkeys
  usrs->hotkeys[HOTKEY_RESTART].active = false;
  usrs->hotkeys[HOTKEY_QUIT].active = false;

  for (int i = 0; i < NUM_HOTKEYS; i++) {
    hotkey* hk = usrs->hotkeys + i;
    bool real_down    = twindow_key_down(env->wnd, hk->key);
    bool real_pressed = tkeyboard_key_pressed(env->kb, hk->key);
    bool fake_down    = (hk->key >= 0 && hk->key < 512) &&
                        gdata->data.fake_key_down[hk->key];
    bool fake_pressed = (hk->key >= 0 && hk->key < 512) &&
                        gdata->data.fake_key_pressed[hk->key];
    if (hk->mode)
      hk->active = real_down || fake_down;
    else
      hk->active ^= (real_pressed || fake_pressed);
  }
  /* Clear fake_key_pressed after processing (one-shot per frame) */
  memset(gdata->data.fake_key_pressed, 0,
         sizeof(gdata->data.fake_key_pressed));

  if (gdata->data.follow_view) {
    snake* me = gdata->data.snakes + (tdarray_length(gdata->data.snakes) - 1);
    int score = (int)floorf((gdata->data.fpsls[me->sct] +
                             me->fam / gdata->data.fmlts[me->sct] - 1) *
                                15 -
                            5) /
                1;
    if (score >= 1000) {
      usrs->hotkeys[HOTKEY_RESTART].active = false;
    }
  }

  gameplay_mode* mode = usrs->modes + usrs->hotkeys[HOTKEY_ASSIST].active;
  if (mode->show_crosshair) igSetMouseCursor(ImGuiMouseCursor_None);
}