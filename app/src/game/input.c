#include <string.h>
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif
#include "input.h"

#include "../user.h"
#include "ntl_team.h"
#include "oef.h"

/* Read by redraw.c. This is a held state (physical or custom on-screen W),
   unlike Vlither's toggle hotkeys, so Skinless Peek feels like NTL. */
bool g_ntl_skin_peek_active = false;

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
      ping_mark_sent(gdata);
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

        float sw = (float)ctx->size[0];
        float sh = (float)ctx->size[1];
        float cx = sw * 0.5f;
        float cy = sh * 0.5f;

        bool touch_down = env->wnd->touch.down;

        if (touch_down) {
          if (env->wnd->touch.just_down || !gdata->touch_ctrl.tp_tracking) {

            float ang = me->eang;
            /* Official mobile starts the arrow 58 scaled pixels ahead of the
               snake, preserving the current direction when a new drag begins. */
            float spawn_r = 58.0f * me->sc * gdata->data.gsc;
            float spawn_x = cx + spawn_r * cosf(ang);
            float spawn_y = cy + spawn_r * sinf(ang);

            gdata->touch_ctrl.tp_tracking     = true;
            gdata->touch_ctrl.tp_visible      = true;
            gdata->touch_ctrl.tp_anchor_x     = spawn_x;
            gdata->touch_ctrl.tp_anchor_y     = spawn_y;
            gdata->touch_ctrl.tp_last_touch_x = tx;
            gdata->touch_ctrl.tp_last_touch_y = ty;
            gdata->touch_ctrl.tp_target_x     = spawn_x;
            gdata->touch_ctrl.tp_target_y     = spawn_y;
            gdata->touch_ctrl.tp_cursor_x     = spawn_x;
            gdata->touch_ctrl.tp_cursor_y     = spawn_y;
          } else {

            float nx = gdata->touch_ctrl.tp_anchor_x
                     + (tx - gdata->touch_ctrl.tp_last_touch_x) * usrs->arrow_sensitivity;
            float ny = gdata->touch_ctrl.tp_anchor_y
                     + (ty - gdata->touch_ctrl.tp_last_touch_y) * usrs->arrow_sensitivity;

            /* Like the official client, the steering target is unrestricted:
               it may cross the snake head. The rendered arrow follows the
               target by 60% per frame, while steering uses the target itself. */
            gdata->touch_ctrl.tp_target_x = nx;
            gdata->touch_ctrl.tp_target_y = ny;
            gdata->touch_ctrl.tp_cursor_x +=
                (nx - gdata->touch_ctrl.tp_cursor_x) * 0.6f;
            gdata->touch_ctrl.tp_cursor_y +=
                (ny - gdata->touch_ctrl.tp_cursor_y) * 0.6f;
          }

          gdata->touch_ctrl.tp_cursor_angle_deg =
              atan2f(cy - gdata->touch_ctrl.tp_cursor_y,
                     cx  - gdata->touch_ctrl.tp_cursor_x) *
              (180.0f / PI);

          xm = (int)(gdata->touch_ctrl.tp_target_x - cx);
          ym = (int)(gdata->touch_ctrl.tp_target_y - cy);

        } else {

          if (gdata->touch_ctrl.tp_tracking) {
            gdata->touch_ctrl.tp_disappear_angle =
                atan2f(gdata->touch_ctrl.tp_cursor_y - cy,
                       gdata->touch_ctrl.tp_cursor_x - cx);
            gdata->touch_ctrl.tp_tracking = false;
            gdata->touch_ctrl.tp_visible  = false;
          }

          xm = (int)(gdata->touch_ctrl.tp_target_x - cx);
          ym = (int)(gdata->touch_ctrl.tp_target_y - cy);
        }

        } else {

        if (env->wnd->touch.down) {
          /* Match Slither mobile's joystick mode: the base is fixed in the
             selected screen-side and the touch's angle from that base steers
             the snake. Unlike Vlither's old stick, the base never follows
             the finger and movement has no distance multiplier. */
          float sw = (float)ctx->size[0];
          float sh = (float)ctx->size[1];
          float margin = sw * 0.025f;
          float jr, jcx, jcy;
          if (usrs->joy_pos_custom) {
            /* Keep the existing slider's scale semantics while converting
               it to Slither's smaller 168px base artwork. */
            jr  = sh * usrs->joy_rel_size * 0.47f;
            jcx = sw * usrs->joy_rel_x;
            jcy = sh * usrs->joy_rel_y;
          } else {
            jr  = sh * 0.082f;
            jcx = usrs->ctrl_swap_sides ? (sw - jr - margin) : (jr + margin);
            jcy = sh - jr - margin;
          }

          float dx = tx - jcx;
          float dy = ty - jcy;
          if (dx * dx + dy * dy > 0.0001f) {
            gdata->touch_ctrl.joy_angle = atan2f(dy, dx);
            gdata->touch_ctrl.joy_has_direction = true;
          }
          gdata->touch_ctrl.joy_tracking = true;

          float steer_len = GLM_MAX(256.0f, jr * 4.0f);
          xm = (int)(cosf(gdata->touch_ctrl.joy_angle) * steer_len);
          ym = (int)(sinf(gdata->touch_ctrl.joy_angle) * steer_len);
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
      /* Official mobile has no centre dead-zone. At the exact head position,
         atan2f(0, 0) resolves to 0 and therefore targets screen-right. */
      ang = atan2f(ym, xm);
      me->eang = ang;
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

  ImGuiIO *input_io = igGetIO_Nil();
  bool typing = input_io && input_io->WantTextInput;
  int active_render_mode = usrs->hotkeys[HOTKEY_ASSIST].active ? 1 : 0;
  g_ntl_skin_peek_active = usrs->mode_skinless_peek[active_render_mode] &&
      !typing &&
      ((physical_keys_enabled && twindow_key_down(env->wnd, GLFW_KEY_W)) ||
       (GLFW_KEY_W < 512 && gdata->data.fake_key_down[GLFW_KEY_W]));
  int shader_key = usrs->shader_cycle_key;
  bool shader_pressed = !typing &&
      ((physical_keys_enabled &&
        tkeyboard_key_pressed(env->kb, shader_key)) ||
       (shader_key >= 0 && shader_key < 512 &&
        gdata->data.fake_key_pressed[shader_key]));
  if (shader_pressed) {
    usrs->shader_cycle_index = (usrs->shader_cycle_index + 1) % 3;
    usrs->modes[0].render_mode = usrs->shader_cycle_index;
    usrs->modes[1].render_mode = usrs->shader_cycle_index;
    save_user_settings(usrs);
  }

  int invisible_key = usrs->invisible_skin_key;
  bool invisible_pressed = !typing &&
      ((physical_keys_enabled &&
        tkeyboard_key_pressed(env->kb, invisible_key)) ||
       (invisible_key >= 0 && invisible_key < 512 &&
        gdata->data.fake_key_pressed[invisible_key]));
  if (invisible_pressed) {
    usrs->own_skin_invisible = !usrs->own_skin_invisible;
    save_user_settings(usrs);
  }

  bool sos_pressed = !typing &&
      ((physical_keys_enabled &&
        tkeyboard_key_pressed(env->kb, GLFW_KEY_S)) ||
       (GLFW_KEY_S < 512 && gdata->data.fake_key_pressed[GLFW_KEY_S]));
  if (sos_pressed) ntl_team_trigger_sos();

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
