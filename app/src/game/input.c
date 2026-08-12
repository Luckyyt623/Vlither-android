#include <string.h>
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif
#include "input.h"

#include "../user.h"
#include "ntl_team.h"

void input(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  struct mg_connection* connection = gdata->connection;
#ifdef ANDROID
  bool physical_keys_enabled = usrs->external_input_mode == 1;
#else
  bool physical_keys_enabled = true;
#endif

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

    if (usrs->hotkeys[HOTKEY_BOT].active ||
        ntl_team_voice_controls_open()) {
      xm = gdata->bot.output.xm;
      ym = gdata->bot.output.ym;
    } else {
      if (physical_keys_enabled && twindow_key_down(env->wnd, GLFW_KEY_LEFT))
        gdata->data.kd_l_frb += gdata->data.vfrb;
      if (physical_keys_enabled && twindow_key_down(env->wnd, GLFW_KEY_RIGHT))
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

      /* Mouse + Keyboard mode uses Android's real USB/Bluetooth mouse
         coordinates when a mouse has been detected. Until then, keep the
         existing touchscreen scheme available as a fallback. */
      bool use_hw_mouse = usrs->external_input_mode == 1 &&
                          env->wnd->hw_mouse_present;

      if (use_hw_mouse) {
        /* A touch-arrow may still have been tracking from the previous
           control mode. Kill that state immediately when hardware-mouse
           steering takes over so it cannot survive as a one-frame/legacy
           second cursor. */
        gdata->touch_ctrl.tp_tracking = false;
        gdata->touch_ctrl.tp_visible = false;

        float cx = (float)ctx->size[0] * 0.5f;
        float cy = (float)ctx->size[1] * 0.5f;
        xm = (int)(env->wnd->hw_mouse_x - cx);
        ym = (int)(env->wnd->hw_mouse_y - cy);
      } else {
        float tx = env->wnd->touch.x;
        float ty = env->wnd->touch.y;

        if (usrs->ctrl_mode_trackpad) {

        #define NTL_FORBIDDEN_R  23.0f
        #define NTL_SPAWN_R      44.0f
        #define NTL_VEL_DECAY    0.85f
        #define NTL_VEL_WEIGHT   0.15f

        float sw = (float)ctx->size[0];
        float sh = (float)ctx->size[1];
        float cx = sw * 0.5f;
        float cy = sh * 0.5f;

        bool touch_down = env->wnd->touch.down;

        if (touch_down) {
          if (env->wnd->touch.just_down || !gdata->touch_ctrl.tp_tracking) {

            float ang = me->eang;
            float spawn_x = cx + NTL_SPAWN_R * cosf(ang);
            float spawn_y = cy + NTL_SPAWN_R * sinf(ang);

            gdata->touch_ctrl.tp_tracking     = true;
            gdata->touch_ctrl.tp_visible      = true;
            gdata->touch_ctrl.tp_anchor_x     = spawn_x;
            gdata->touch_ctrl.tp_anchor_y     = spawn_y;
            gdata->touch_ctrl.tp_last_touch_x = tx;
            gdata->touch_ctrl.tp_last_touch_y = ty;
            gdata->touch_ctrl.tp_cursor_x     = spawn_x;
            gdata->touch_ctrl.tp_cursor_y     = spawn_y;
            gdata->touch_ctrl.tp_vx           = 0.0f;
            gdata->touch_ctrl.tp_vy           = 0.0f;
          } else {

            float nx = gdata->touch_ctrl.tp_anchor_x
                     + (tx - gdata->touch_ctrl.tp_last_touch_x) * usrs->arrow_sensitivity;
            float ny = gdata->touch_ctrl.tp_anchor_y
                     + (ty - gdata->touch_ctrl.tp_last_touch_y) * usrs->arrow_sensitivity;

            nx = GLM_MAX(0.0f, GLM_MIN(sw, nx));
            ny = GLM_MAX(0.0f, GLM_MIN(sh, ny));

            float fdx  = nx - cx;
            float fdy  = ny - cy;
            float dist = sqrtf(fdx * fdx + fdy * fdy);
            if (dist < NTL_FORBIDDEN_R && dist > 0.001f) {
              float a = atan2f(fdy, fdx);
              nx = cx + cosf(a) * NTL_FORBIDDEN_R;
              ny = cy + sinf(a) * NTL_FORBIDDEN_R;
            }

            float mdx = nx - gdata->touch_ctrl.tp_cursor_x;
            float mdy = ny - gdata->touch_ctrl.tp_cursor_y;
            gdata->touch_ctrl.tp_vx =
                gdata->touch_ctrl.tp_vx * NTL_VEL_DECAY + mdx * NTL_VEL_WEIGHT;
            gdata->touch_ctrl.tp_vy =
                gdata->touch_ctrl.tp_vy * NTL_VEL_DECAY + mdy * NTL_VEL_WEIGHT;

            gdata->touch_ctrl.tp_cursor_x = nx;
            gdata->touch_ctrl.tp_cursor_y = ny;
          }

          gdata->touch_ctrl.tp_cursor_angle_deg =
              atan2f(cy - gdata->touch_ctrl.tp_cursor_y,
                     cx  - gdata->touch_ctrl.tp_cursor_x) *
              (180.0f / PI);

          xm = (int)(gdata->touch_ctrl.tp_cursor_x - cx);
          ym = (int)(gdata->touch_ctrl.tp_cursor_y - cy);

        } else {

          if (gdata->touch_ctrl.tp_tracking) {
            gdata->touch_ctrl.tp_disappear_angle =
                atan2f(gdata->touch_ctrl.tp_cursor_y - cy,
                       gdata->touch_ctrl.tp_cursor_x - cx);
            gdata->touch_ctrl.tp_tracking = false;
            gdata->touch_ctrl.tp_visible  = false;
          }

          xm = (int)(gdata->touch_ctrl.tp_cursor_x - cx);
          ym = (int)(gdata->touch_ctrl.tp_cursor_y - cy);
        }

        } else {

        if (env->wnd->touch.down) {

          if (env->wnd->touch.just_down || !gdata->touch_ctrl.joy_tracking) {
            gdata->touch_ctrl.joy_anchor_x = tx;
            gdata->touch_ctrl.joy_anchor_y = ty;
            gdata->touch_ctrl.joy_tracking = true;
          }

          xm = (int)((tx - gdata->touch_ctrl.joy_anchor_x) * 4.0f);
          ym = (int)((ty - gdata->touch_ctrl.joy_anchor_y) * 4.0f);

          gdata->touch_ctrl.joy_last_xm = xm;
          gdata->touch_ctrl.joy_last_ym = ym;
        } else {
          if (!env->wnd->touch.down) gdata->touch_ctrl.joy_tracking = false;

          xm = gdata->touch_ctrl.joy_last_xm;
          ym = gdata->touch_ctrl.joy_last_ym;
        }
        }
      }
#else
      xm = (int)env->ms->pos[0] - ctx->size[0] / 2;
      ym = (int)env->ms->pos[1] - ctx->size[1] / 2;
#endif
    }
#ifdef ANDROID
    bool external_boost = false;
    if (usrs->external_input_mode == 1) {
      /* Primary/left mouse button is the external-mouse boost control.
         Space/Up are also accepted from a physical keyboard, matching the
         desktop control vocabulary. */
      external_boost =
          twindow_button_down(env->wnd, GLFW_MOUSE_BUTTON_LEFT) ||
          tmouse_button_pressed(env->ms, GLFW_MOUSE_BUTTON_LEFT) ||
          twindow_key_down(env->wnd, GLFW_KEY_SPACE) ||
          twindow_key_down(env->wnd, GLFW_KEY_UP) ||
          tkeyboard_key_pressed(env->kb, GLFW_KEY_SPACE) ||
          tkeyboard_key_pressed(env->kb, GLFW_KEY_UP);
    }
    gdata->data.wmd = env->wnd->touch.boost_down || external_boost ||
                      gdata->bot.output.accel;
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

#ifdef ANDROID
  if (physical_keys_enabled)
    gdata->data.ms_zoom *= expf(env->ms->dwheel * usrs->zoom_step);
#else
  gdata->data.ms_zoom *= expf(env->ms->dwheel * usrs->zoom_step);
#endif

  if ((physical_keys_enabled && tkeyboard_key_pressed(env->kb, GLFW_KEY_N)) ||
      (GLFW_KEY_N < 512 && gdata->data.fake_key_pressed[GLFW_KEY_N]))
    gdata->data.ms_zoom *= expf(1 * usrs->zoom_step);
  else if ((physical_keys_enabled && tkeyboard_key_pressed(env->kb, GLFW_KEY_M)) ||
           (GLFW_KEY_M < 512 && gdata->data.fake_key_pressed[GLFW_KEY_M]))
    gdata->data.ms_zoom *= expf(-1 * usrs->zoom_step);

  gdata->data.ms_zoom =
      GLM_MAX(MAX_ZOOM_OUT, GLM_MIN(gdata->data.ms_zoom, MAX_ZOOM_IN));

  /* V owns the voice-controls overlay. Check both a physical keyboard and a
     custom on-screen V key before fake key presses are cleared below. */
  bool voice_key_pressed =
      (physical_keys_enabled &&
       tkeyboard_key_pressed(env->kb, GLFW_KEY_V)) ||
      (GLFW_KEY_V < 512 && gdata->data.fake_key_pressed[GLFW_KEY_V]);
  if (voice_key_pressed) ntl_team_handle_voice_key();

  usrs->hotkeys[HOTKEY_RESTART].active = false;
  usrs->hotkeys[HOTKEY_QUIT].active = false;

  for (int i = 0; i < NUM_HOTKEYS; i++) {
    hotkey* hk = usrs->hotkeys + i;
    bool real_down    = physical_keys_enabled && twindow_key_down(env->wnd, hk->key);
    bool real_pressed = physical_keys_enabled && tkeyboard_key_pressed(env->kb, hk->key);
    bool fake_down    = (hk->key >= 0 && hk->key < 512) &&
                        gdata->data.fake_key_down[hk->key];
    bool fake_pressed = (hk->key >= 0 && hk->key < 512) &&
                        gdata->data.fake_key_pressed[hk->key];
    if (hk->mode)
      hk->active = real_down || fake_down;
    else
      hk->active ^= (real_pressed || fake_pressed);
  }

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

  /* show_crosshair is now the persisted local head-dot setting. It no longer
     represents the OS mouse cursor, so never hide a real mouse because the
     head dot is enabled. */
}
