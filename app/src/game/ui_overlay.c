#ifdef ANDROID
#include "../android_glfw_shim.h"

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) \
  (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0))
#endif
#endif
#include "ui_overlay.h"

#include <math.h>
#include "../user.h"
#include "user_settings.h"
#include "sbot.h"

void ui_overlay(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  float mww2 = ctx->size[0] / 2.0f;
  float mhh2 = ctx->size[1] / 2.0f;

  int snakes_len = tdarray_length(gdata->data.snakes);
  if (snakes_len) {
    snake* me = gdata->data.snakes + (snakes_len - 1);

    if (gdata->data.snake_id == me->id) {
      float a = me->alive_amt * (1 - me->dead_amt);
      int sct = me->sct + me->rsc;
      float hx = me->xx + me->fx;
      float hy = me->yy + me->fy;
      gdata->data.score = (int)floorf((gdata->data.fpsls[sct] +
                                       me->fam / gdata->data.fmlts[sct] - 1) *
                                          15 -
                                      5) /
                          1;

      if (usrs->hotkeys[HOTKEY_ASSIST].active) {

#ifdef ANDROID
        float laser_tx = gdata->touch_ctrl.tp_cursor_x;
        float laser_ty = gdata->touch_ctrl.tp_cursor_y;

        if (laser_tx == 0.0f && laser_ty == 0.0f) {
          laser_tx = mww2;
          laser_ty = mhh2;
        }
#else
        float laser_tx = env->ms->pos[0];
        float laser_ty = env->ms->pos[1];
#endif
        ImDrawList_AddLine(
            igGetWindowDrawList(),
            (ImVec2){mww2 + (hx - gdata->data.view_xx) * gdata->data.gsc,
                     mhh2 + (hy - gdata->data.view_yy) * gdata->data.gsc},
            (ImVec2){laser_tx, laser_ty},
            igColorConvertFloat4ToU32(
                (ImVec4){usrs->laser_color[0], usrs->laser_color[1],
                         usrs->laser_color[2], usrs->laser_color[3] * a}),
            usrs->laser_thickness);
      }
    }
  }

  usr->r->global.minimap_opacity = 0;
  if (usrs->hotkeys[HOTKEY_HUD].active) {
    ImGuiStyle* style = igGetStyle();
    float frame_height = igGetFrameHeight();

    igPushFont(usr->imgui_data.mono_font[usrs->stats_font_size],
               usr->imgui_data.mono_font[usrs->stats_font_size]->LegacySize);
    float line_height = igGetCursorPosY();
    ImVec2 icon_sz;
    igCalcTextSize(&icon_sz, "\ue971", NULL, false, -1);
    ImVec2 char_sz;
    igCalcTextSize(&char_sz, "-", NULL, false, -1);
    igTextColored((ImVec4){1, 1, 1, 0.3}, "\ue971");
    igSameLine(0, -1);
    igTextColored((ImVec4){1, 1, 1, 0.5}, usrs->nickname);
    line_height = igGetCursorPosY() - line_height;

    igTextColored((ImVec4){1, 1, 1, 0.3}, "\ueaec");
    igSameLine(0, -1);
    igTextColored((ImVec4){1, 1, 1, 0.5}, usrs->ipv4);

    float ping_norm =
        (gdata->data.ping_follow - GOOD_PING) / (BAD_PING - GOOD_PING);
    float lag_norm = (gdata->data.lag_mult - 0.2f) / (1 - 0.2f);
    vec3 ping_col;
    glm_vec3_lerp((vec3){0.5f, 1, 0.5f}, (vec3){1, 0.5f, 0.5f}, ping_norm,
                  ping_col);
    vec3 ic_col;
    glm_vec3_lerp((vec3){1, 0.5f, 0.5f}, (vec3){1, 1, 1}, lag_norm, ic_col);

    igTextColored(
        (ImVec4){ic_col[0], ic_col[1], ic_col[2], glm_lerp(0.8, 0.3, lag_norm)},
        "\ue91b");
    igSameLine(0, -1);
    igTextColored(
        (ImVec4){ping_col[0], ping_col[1], ping_col[2], 0.6 * lag_norm},
        "%d ms", gdata->data.ping);

    igTextColored((ImVec4){1, 1, 1, 0.3}, "\ue99c");
    igSameLine(0, -1);
    igTextColored((ImVec4){1, 1, 1, 0.5}, "%d FPS", gdata->data.fps);

    igTextColored((ImVec4){1, 1, 1, 0.3}, "\ue952");
    igSameLine(0, -1);

    int tot_sec = (int)gdata->data.play_etm;
    int hours = tot_sec / 3600;
    int minutes = (tot_sec % 3600) / 60;
    int seconds = tot_sec % 60;

    igTextColored((ImVec4){1, 1, 1, 0.7}, "%02d:%02d:%02d", hours, minutes,
                  seconds);
    igText("");

    if (usrs->hotkeys[HOTKEY_MENU].active) {
      display_hotkeys(usr, (icon_sz.x - char_sz.x) * 0.5f,
                      usrs->stats_font_size);

    }

    float px = (((gdata->data.view_xx - gdata->data.grd) * 2) /
                ((gdata->data.flux_grd) * 2));
    float py = (((gdata->data.view_yy - gdata->data.grd) * 2) /
                ((gdata->data.flux_grd) * 2));
    int pang = (int)roundf(glm_deg(atan2f(-py, px)));
    if (pang < 0) pang += 360;
    int dst = (int)roundf(sqrtf(px * px + py * py) * 100.0f);

    igSetCursorPosY(ctx->size[1] - (line_height * 3) - style->WindowPadding.y);

    igTextColored((ImVec4){1, 1, 1, 0.3}, "\ueaeb");
    igSameLine(0, -1);
    igTextColored((ImVec4){1, 1, 1, 0.7}, "%d", gdata->data.kills);

    igTextColored((ImVec4){1, 1, 1, 0.3}, "\ue9d9");
    igSameLine(0, -1);

    igTextColored((ImVec4){1, 1, 1, 0.7}, "%d", gdata->data.rank);

    igSameLine(0, 0);
    igTextColored((ImVec4){1, 1, 1, 0.5}, " / %d", gdata->data.slither_count);

    igTextColored((ImVec4){1, 1, 1, 0.3}, "\ue99e");
    igSameLine(0, -1);
    igPushFont(
        usr->imgui_data.mono_font_bold[usrs->stats_font_size],
        usr->imgui_data.mono_font_bold[usrs->stats_font_size]->LegacySize);
    igTextColored((ImVec4){1, 1, 1, 0.7}, "%d", gdata->data.score);
    igPopFont();

    igPopFont();

    if (gdata->data.gotlb) {
      igPushFont(usr->imgui_data.mono_font[usrs->lb_font_size],
                 usr->imgui_data.mono_font[usrs->lb_font_size]->LegacySize);
      ImVec2 psize;
      igCalcTextSize(&psize, "10.", NULL, false, -1);

      ImVec2 nksize;
      char tmp[MAX_NICKNAME_LEN + 1] = {0};
      memset(tmp, (int)'a', MAX_NICKNAME_LEN);
      igCalcTextSize(&nksize, tmp, NULL, false, -1);
      nksize.x *= 1.25f;

      ImVec2 scsize;
      igCalcTextSize(&scsize, "999999", NULL, false, -1);
      igPopFont();

      float tb_width =
          psize.x + nksize.x + scsize.x + (style->CellPadding.x * 2 * 3);
      igSetCursorPosX(ctx->size[0] - tb_width - style->WindowPadding.x);
      igSetCursorPosY(style->WindowPadding.y);

      if (igBeginTable("leaderboard_table", 3, ImGuiTableFlags_NoHostExtendX,
                       (ImVec2){}, 0)) {
        igTableSetupColumn("##position", ImGuiTableColumnFlags_WidthFixed,
                           psize.x, 0);
        igTableSetupColumn("##nickname", ImGuiTableColumnFlags_WidthFixed,
                           nksize.x, 0);
        igTableSetupColumn("##score", ImGuiTableColumnFlags_WidthFixed,
                           scsize.x, 0);

        for (int row = 0; row < NUM_LEADERBOARD_ENTRIES; row++) {
          bool is_my_snake = gdata->data.lb_pos == (row + 1);
          vec3s* scolor = gdata->cg_colors + gdata->data.lb.entries[row].cv;
          vec3 tcolor;
          glm_vec3_lerp((float*)scolor, (vec3){1, 1, 1}, 0.4f, tcolor);
          ImVec4 itcolor = {tcolor[0], tcolor[1], tcolor[2], 1};
          if (is_my_snake) {
            igPushFont(
                usr->imgui_data.mono_font_bold[usrs->lb_font_size],
                usr->imgui_data.mono_font_bold[usrs->lb_font_size]->LegacySize);
          } else {
            igPushFont(
                usr->imgui_data.mono_font[usrs->lb_font_size],
                usr->imgui_data.mono_font[usrs->lb_font_size]->LegacySize);
            itcolor.w = 0.6f;
          }

          igTableNextRow(ImGuiTableRowFlags_None, 0);
          igTableSetColumnIndex(0);
          igTextColored((ImVec4){1, 1, 1, itcolor.w}, "%2d.", row + 1);
          igTableSetColumnIndex(1);
          igTextColored(itcolor, "%s", gdata->data.lb.entries[row].nickname);
          igTableSetColumnIndex(2);
          igTextColored(itcolor, "%d", gdata->data.lb.entries[row].score);

          igPopFont();
        }
        igEndTable();
      }
    }

    usr->r->global.minimap_circ[2] = usrs->minimap_size;
    usr->r->global.minimap_circ[0] =
        ctx->size[0] - usr->r->global.minimap_circ[2] - style->WindowPadding.x;
    usr->r->global.minimap_circ[1] = ctx->size[1] -
                                     usr->r->global.minimap_circ[2] -
                                     style->WindowPadding.y - line_height;
    usr->r->global.minimap_opacity = 1;

    igPushFont(usr->imgui_data.mono_font[usrs->stats_font_size],
               usr->imgui_data.mono_font[usrs->stats_font_size]->LegacySize);
    ImVec2 lctxtsz;
    igCalcTextSize(&lctxtsz, "--360° 100%", NULL, false, -1);

    igSetCursorPosX(
        ctx->size[0] -
        (usr->r->global.minimap_circ[2] * 0.5 + style->WindowPadding.x) -
        lctxtsz.x * 0.5f);
    igSetCursorPosY(ctx->size[1] - style->WindowPadding.y - line_height);

    igTextColored((ImVec4){1, 1, 1, 0.3f}, "\ue947");
    igSameLine(0, -1);
    igTextColored((ImVec4){1, 1, 1, 0.7f}, "%d° %d%%", pang, dst);
    igPopFont();
  }

#ifdef ANDROID

  { extern bool g_overlay_drawn_this_frame; g_overlay_drawn_this_frame = true; }

  extern bool g_ctrl_swap_sides;
  g_ctrl_swap_sides = usrs->ctrl_swap_sides;

  if (gdata->data.follow_view) {
    ImDrawList* dl  = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
    float sw        = (float)ctx->size[0];
    float sh        = (float)ctx->size[1];
    float margin    = sw * 0.025f;

    bool  boost_on  = env->wnd->touch.boost_down;
    bool  swapped   = usrs->ctrl_swap_sides;

    extern float g_zslider_left, g_zslider_top, g_zslider_right, g_zslider_bottom;
    extern float g_zslider_half_h;
    extern float g_zoom_sensitivity;
    g_zoom_sensitivity = usrs->zoom_sensitivity;

    float br, bcx, bcy;
    if (usrs->boost_pos_custom) {
        br  = sh * usrs->boost_rel_size;
        bcx = sw * usrs->boost_rel_x;
        bcy = sh * usrs->boost_rel_y;
    } else {
        br  = sh * 0.125f;
        bcx = swapped ? (br + margin) : (sw - br - margin);
        bcy = sh - br - margin;
    }

    float jr, jcx, jcy;
    if (usrs->joy_pos_custom) {
        jr  = sh * usrs->joy_rel_size;
        jcx = sw * usrs->joy_rel_x;
        jcy = sh * usrs->joy_rel_y;
    } else {
        jr  = sh * 0.175f;
        jcx = swapped ? (sw - jr - margin) : (jr + margin);
        jcy = sh - jr - margin;
    }

    { extern float g_boost_cx,g_boost_cy,g_boost_r,g_joy_cx,g_joy_cy,g_joy_r;
      extern bool  g_is_trackpad_mode, g_panel_open;
      g_boost_cx = bcx; g_boost_cy = bcy; g_boost_r = br;
      g_joy_cx   = jcx; g_joy_cy   = jcy; g_joy_r   = jr;
      g_is_trackpad_mode = usrs->ctrl_mode_trackpad;
      (void)g_panel_open;  }

    if (!usrs->ctrl_mode_trackpad) {
      bool  joy_on = gdata->touch_ctrl.joy_tracking;

      float jo = usrs->joy_opacity;
      ImDrawList_AddCircle(dl,
        (ImVec2){jcx, jcy}, jr,
        IM_COL32(255,255,255,(int)((joy_on?70:38)*jo)), 64, 3.0f);
      ImDrawList_AddCircle(dl,
        (ImVec2){jcx, jcy}, jr * 0.32f,
        IM_COL32(255,255,255,(int)(18*jo)), 32, 1.5f);

      ImU32 cross = IM_COL32(255,255,255,(int)(18*jo));
      ImDrawList_AddLine(dl,
        (ImVec2){jcx - jr * 0.78f, jcy}, (ImVec2){jcx + jr * 0.78f, jcy}, cross, 1.5f);
      ImDrawList_AddLine(dl,
        (ImVec2){jcx, jcy - jr * 0.78f}, (ImVec2){jcx, jcy + jr * 0.78f}, cross, 1.5f);

      float jtx = jcx, jty = jcy;
      static float s_joy_last_dx = 0.0f, s_joy_last_dy = 0.0f;
      if (joy_on) {
        float dx   = env->wnd->touch.x - gdata->touch_ctrl.joy_anchor_x;
        float dy   = env->wnd->touch.y - gdata->touch_ctrl.joy_anchor_y;
        float dist = sqrtf(dx * dx + dy * dy);
        float cap  = jr * 0.68f;
        float sc   = (dist > cap && dist > 0.001f) ? cap / dist : 1.0f;
        jtx = jcx + dx * sc;
        jty = jcy + dy * sc;
        s_joy_last_dx = jtx - jcx;
        s_joy_last_dy = jty - jcy;
      } else {

        jtx = jcx + s_joy_last_dx;
        jty = jcy + s_joy_last_dy;
      }
      ImDrawList_AddCircleFilled(dl,
        (ImVec2){jtx, jty}, jr * 0.29f,
        joy_on ? IM_COL32(255, 255, 255, 210) : IM_COL32(255, 255, 255, 60), 32);

      float hint_sz = jr * 0.22f;
      ImVec2 mv_sz; igCalcTextSize(&mv_sz, "MOVE", NULL, false, -1.0f);
      float mv_scale = hint_sz / igGetFontSize();
      ImDrawList_AddText_FontPtr(dl, igGetFont(), hint_sz,
        (ImVec2){jcx - mv_sz.x * mv_scale * 0.5f, jcy + jr + 4},
        IM_COL32(255, 255, 255, 65), "MOVE", NULL, 0.0f, NULL);

    } else {

      float ar = 0.776f, ag = 0.263f, ab = 0.310f;
      {
        int sl2 = tdarray_length(gdata->data.snakes);
        if (sl2 > 0) {
          snake* mptr = gdata->data.snakes + (sl2 - 1);
          if (gdata->data.snake_id == mptr->id) {
            int cv = mptr->cv;
            if (cv < 0) cv = 0;
            if (cv >= NUM_COLOR_GROUPS) cv = NUM_COLOR_GROUPS - 1;
            vec3s* sc = gdata->cg_colors + cv;
            ar = 0.25f + 0.75f * sc->r;
            ag = 0.25f + 0.75f * sc->g;
            ab = 0.25f + 0.75f * sc->b;
            if (ar > 1.0f) ar = 1.0f;
            if (ag > 1.0f) ag = 1.0f;
            if (ab > 1.0f) ab = 1.0f;
          }
        }
      }

      static float s_accel_a  = 0.0f;
      static float s_accel_fr = 0.0f;
      bool is_boosting = boost_on;
      float vfr2 = gdata->data.vfr > 0.0f ? gdata->data.vfr : 1.0f;

      if (is_boosting) {
        s_accel_a += vfr2 * 0.03f;
        if (s_accel_a > 1.0f) s_accel_a = 1.0f;
      } else {
        s_accel_a -= vfr2 * 0.03f;
        if (s_accel_a < 0.0f) s_accel_a = 0.0f;
      }
      s_accel_fr += vfr2 * 0.22f;

      if (gdata->touch_ctrl.tp_tracking && gdata->touch_ctrl.tp_visible
          && !usrs->hotkeys[HOTKEY_BOT].active && !usrs->arrow_invisible) {
        float acx = gdata->touch_ctrl.tp_cursor_x;
        float acy = gdata->touch_ctrl.tp_cursor_y;

        float dir_angle = atan2f(sh * 0.5f - acy, sw * 0.5f - acx);
        float rot  = dir_angle;
        float cs   = cosf(rot);
        float sn_v = sinf(rot);

        float boost_sz = 1.0f + 0.5f * s_accel_a;
        float aw = sh * 0.11f  * usrs->arrow_size * boost_sz;
        float ah = sh * 0.066f * usrs->arrow_size * boost_sz;

        #define ARPT(px, py) \
          (ImVec2){ acx + (px)*cs - (py)*sn_v, \
                    acy + (px)*sn_v + (py)*cs }

        ImU32 fill_col   = IM_COL32((int)(ar*255),(int)(ag*255),(int)(ab*255), 230);

        ImU32 border_col = IM_COL32((int)(ar*178),(int)(ag*178),(int)(ab*178), 255);

        ImVec2 body[4] = {
          ARPT(-0.10f*aw, -0.25f*ah),
          ARPT( 0.50f*aw, -0.25f*ah),
          ARPT( 0.50f*aw,  0.25f*ah),
          ARPT(-0.10f*aw,  0.25f*ah),
        };
        ImDrawList_AddConvexPolyFilled(dl, body, 4, fill_col);

        ImDrawList_AddTriangleFilled(dl,
          ARPT(-0.10f*aw, -0.50f*ah),
          ARPT(-0.10f*aw, -0.25f*ah),
          ARPT(-0.50f*aw,  0.00f   ),
          fill_col);

        ImDrawList_AddTriangleFilled(dl,
          ARPT(-0.10f*aw,  0.25f*ah),
          ARPT(-0.10f*aw,  0.50f*ah),
          ARPT(-0.50f*aw,  0.00f   ),
          fill_col);

        ImVec2 outline[7] = {
          ARPT(-0.10f*aw, -0.50f*ah),
          ARPT(-0.10f*aw, -0.25f*ah),
          ARPT( 0.50f*aw, -0.25f*ah),
          ARPT( 0.50f*aw,  0.25f*ah),
          ARPT(-0.10f*aw,  0.25f*ah),
          ARPT(-0.10f*aw,  0.50f*ah),
          ARPT(-0.50f*aw,  0.00f   ),
        };
        ImDrawList_AddPolyline(dl, outline, 7, border_col,
          ImDrawFlags_Closed, 3.0f);

        if (s_accel_a > 0.0f && usrs->boost_arrow_anim) {
          float pulse = s_accel_a * (0.5f + 0.5f * cosf(s_accel_fr));
          int   ga    = (int)(pulse * 200.0f);
          if (ga > 0) {
            ImU32 glow_col = IM_COL32((int)(ar*255),(int)(ag*255),(int)(ab*255), ga);
            float gaw = aw * 1.15f;
            float gah = ah * 1.15f;

            ImVec2 gbody[4] = {
              ARPT(-0.10f*gaw, -0.25f*gah),
              ARPT( 0.50f*gaw, -0.25f*gah),
              ARPT( 0.50f*gaw,  0.25f*gah),
              ARPT(-0.10f*gaw,  0.25f*gah),
            };
            ImDrawList_AddConvexPolyFilled(dl, gbody, 4, glow_col);
            ImDrawList_AddTriangleFilled(dl,
              ARPT(-0.10f*gaw, -0.50f*gah),
              ARPT(-0.10f*gaw, -0.25f*gah),
              ARPT(-0.50f*gaw,  0.00f    ), glow_col);
            ImDrawList_AddTriangleFilled(dl,
              ARPT(-0.10f*gaw,  0.25f*gah),
              ARPT(-0.10f*gaw,  0.50f*gah),
              ARPT(-0.50f*gaw,  0.00f    ), glow_col);
          }
        }

        #undef ARPT
      }
    }

    {

      float bbr = 0.863f, bbg = 0.314f, bbb = 0.216f;
      {
        int sl3 = tdarray_length(gdata->data.snakes);
        if (sl3 > 0) {
          snake* mbptr = gdata->data.snakes + (sl3 - 1);
          if (gdata->data.snake_id == mbptr->id) {
            int cv2 = mbptr->cv;
            if (cv2 < 0) cv2 = 0;
            if (cv2 >= NUM_COLOR_GROUPS) cv2 = NUM_COLOR_GROUPS - 1;
            vec3s* sc2 = gdata->cg_colors + cv2;
            bbr = 0.25f + 0.75f * sc2->r; if (bbr > 1.0f) bbr = 1.0f;
            bbg = 0.25f + 0.75f * sc2->g; if (bbg > 1.0f) bbg = 1.0f;
            bbb = 0.25f + 0.75f * sc2->b; if (bbb > 1.0f) bbb = 1.0f;
          }
        }
      }

      static float s_boost_a = 0.3f;
      float vfr3 = gdata->data.vfr > 0.0f ? gdata->data.vfr : 1.0f;
      if (boost_on) { s_boost_a += vfr3*0.05f; if (s_boost_a>0.6f) s_boost_a=0.6f; }
      else          { s_boost_a -= vfr3*0.05f; if (s_boost_a<0.3f) s_boost_a=0.3f; }

      float eff_a = s_boost_a * usrs->boost_opacity;
      float hr = bbr*1.3f>1.0f?1.0f:bbr*1.3f;
      float hg = bbg*1.3f>1.0f?1.0f:bbg*1.3f;
      float hb = bbb*1.3f>1.0f?1.0f:bbb*1.3f;
      ImU32 fill_c = IM_COL32((int)(bbr*255),(int)(bbg*255),(int)(bbb*255),(int)(eff_a*255));
      float ring_a = eff_a*1.1f>1.0f?1.0f:eff_a*1.1f;
      ImU32 ring_c = IM_COL32((int)(hr*255),(int)(hg*255),(int)(hb*255),(int)(ring_a*255));

      ImDrawList_AddCircleFilled(dl, (ImVec2){bcx,bcy}, br, fill_c, 48);
      ImDrawList_AddCircle      (dl, (ImVec2){bcx,bcy}, br, ring_c, 48, 3.0f);
    }

    float bfont = br * 0.40f;
    ImVec2 bt_sz; igCalcTextSize(&bt_sz, "BOOST", NULL, false, -1.0f);
    float bt_scale = bfont / igGetFontSize();
    ImDrawList_AddText_FontPtr(dl, igGetFont(), bfont,
      (ImVec2){bcx - bt_sz.x * bt_scale * 0.5f, bcy - bfont * 0.5f},
      IM_COL32(255, 255, 255, boost_on ? 255 : 200),
      "BOOST", NULL, 0.0f, NULL);

  }

  if (gdata->data.follow_view) {
    float sh = (float)ctx->size[1];
    float sw = (float)ctx->size[0];
    static const char* hk_short[NUM_HOTKEYS] = {
      "HUD","Names","BigF","Asst","Bot","Menu","Restart","Quit"
    };
    ImDrawList* hkdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
    float btn_h  = sh * 0.065f;
    float btn_w  = sw * 0.130f;
    float margin2 = sw * 0.012f;
    float start_y = sh * 0.012f;
    int   hk_col  = 0;

    for (int hi = 0; hi < NUM_HOTKEYS; hi++) {
      if (!usrs->hk_show_btn[hi]) continue;
      float bx = margin2 + (float)hk_col * (btn_w + margin2);
      float by = start_y;
      hk_col++;
      bool is_on = usrs->hotkeys[hi].active;
      ImU32 hk_bg  = is_on ? IM_COL32(55,175,75,200)  : IM_COL32(30,30,50,170);
      ImU32 hk_brd = is_on ? IM_COL32(100,220,120,230): IM_COL32(140,140,160,180);
      ImU32 hk_txt = IM_COL32(255,255,255, is_on ? 240 : 180);
      float rnd    = btn_h * 0.22f;
      ImDrawList_AddRectFilled(hkdl,
        (ImVec2){bx, by}, (ImVec2){bx+btn_w, by+btn_h}, hk_bg,  rnd, 0);
      ImDrawList_AddRect(hkdl,
        (ImVec2){bx, by}, (ImVec2){bx+btn_w, by+btn_h}, hk_brd, rnd, 0, 1.5f);
      float fsz = btn_h * 0.40f;
      float tsc = fsz / igGetFontSize();
      ImVec2 tsz; igCalcTextSize(&tsz, hk_short[hi], NULL, false, -1.0f);
      ImDrawList_AddText_FontPtr(hkdl, igGetFont(), fsz,
        (ImVec2){bx + btn_w*0.5f - tsz.x*tsc*0.5f, by + btn_h*0.5f - fsz*0.5f},
        hk_txt, hk_short[hi], NULL, 0, NULL);

      ImGuiIO* hkio = igGetIO_Nil();
      if (hkio && igIsMouseClicked_Bool(0, false)) {
        float mx = hkio->MousePos.x, my = hkio->MousePos.y;
        if (mx >= bx && mx <= bx+btn_w && my >= by && my <= by+btn_h)
          usrs->hotkeys[hi].active = !usrs->hotkeys[hi].active;
      }
    }
  }

  if (gdata->data.follow_view) {

    extern float g_zslider_left, g_zslider_top, g_zslider_right, g_zslider_bottom;
    extern float g_zslider_half_h;
    extern bool  g_zslider_horizontal;

    float sw2 = (float)ctx->size[0];
    float sh2 = (float)ctx->size[1];

    float zs_half_h = sh2 * usrs->zslider_rel_h;
    float zs_half_w = sh2 * 0.022f;
    float zs_cx     = sw2 * usrs->zslider_rel_x;
    float zs_cy     = sh2 * usrs->zslider_rel_y;

    g_zslider_horizontal = usrs->zslider_horizontal;

    if (usrs->zslider_hidden) {

      g_zslider_left = g_zslider_right = g_zslider_top = g_zslider_bottom = 0;
      g_zslider_half_h = 1;
    } else {
      ImDrawList* zdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
      float zopa = usrs->zslider_opacity;
      ImU32 track_col = IM_COL32(255, 255, 255, (int)(50  * zopa));
      ImU32 track_brd = IM_COL32(255, 255, 255, (int)(90  * zopa));
      ImU32 thumb_col = IM_COL32(255, 255, 255, (int)(180 * zopa));
      ImU32 thumb_brd = IM_COL32(255, 255, 255, (int)(230 * zopa));

      static float s_thumb_vis_offset = 0.0f;
      float target_offset = env->wnd->touch.zslider_offset;
      s_thumb_vis_offset += (target_offset - s_thumb_vis_offset) * 0.25f;
      bool  touching = (env->wnd->touch.zslider_ptr_id != -1);

      if (touching && target_offset != 0.0f) {
        float half = zs_half_h > 1.0f ? zs_half_h : 1.0f;
        float norm = target_offset / half;
        if (norm >  1.0f) norm =  1.0f;
        if (norm < -1.0f) norm = -1.0f;

        gdata->data.ms_zoom *= expf(norm * 4.0f
                                    * usrs->zoom_sensitivity
                                    * usrs->zoom_step);
        if (gdata->data.ms_zoom > MAX_ZOOM_IN)  gdata->data.ms_zoom = MAX_ZOOM_IN;
        if (gdata->data.ms_zoom < MAX_ZOOM_OUT) gdata->data.ms_zoom = MAX_ZOOM_OUT;
      }

      ImU32 t_fill = touching ? IM_COL32(180,220,255,(int)(220*zopa))
                               : IM_COL32(255,255,255,(int)(160*zopa));

      if (usrs->zslider_horizontal) {

        float half_l = zs_half_h;
        float half_t = zs_half_w;

        g_zslider_left   = zs_cx - half_l * 1.5f;
        g_zslider_right  = zs_cx + half_l * 1.5f;
        g_zslider_top    = zs_cy - half_t * 1.5f;
        g_zslider_bottom = zs_cy + half_t * 1.5f;
        g_zslider_half_h = half_l;

        ImDrawList_AddRectFilled(zdl,
          (ImVec2){zs_cx - half_l, zs_cy - half_t * 0.18f},
          (ImVec2){zs_cx + half_l, zs_cy + half_t * 0.18f},
          track_col, half_t * 0.18f, 0);

        float lbl_sz = half_t * 1.1f;
        ImVec2 lbl_m_sz; igCalcTextSize(&lbl_m_sz, "-", NULL, false, -1.0f);
        ImVec2 lbl_p_sz; igCalcTextSize(&lbl_p_sz, "+", NULL, false, -1.0f);
        float  lbl_sc = lbl_sz / igGetFontSize();
        ImDrawList_AddText_FontPtr(zdl, igGetFont(), lbl_sz,
          (ImVec2){zs_cx - half_l - lbl_m_sz.x * lbl_sc - 4,
                   zs_cy - lbl_sz * 0.5f},
          IM_COL32(255,255,255,(int)(120*zopa)), "-", NULL, 0, NULL);
        ImDrawList_AddText_FontPtr(zdl, igGetFont(), lbl_sz,
          (ImVec2){zs_cx + half_l + 4, zs_cy - lbl_sz * 0.5f},
          IM_COL32(255,255,255,(int)(120*zopa)), "+", NULL, 0, NULL);

        float thumb_x = zs_cx + s_thumb_vis_offset;
        float thumb_hw = half_t * 1.4f;
        float thumb_hh = half_t * 1.1f;
        ImDrawList_AddRectFilled(zdl,
          (ImVec2){thumb_x - thumb_hw, zs_cy - thumb_hh},
          (ImVec2){thumb_x + thumb_hw, zs_cy + thumb_hh},
          t_fill, thumb_hw * 0.5f, 0);
        ImDrawList_AddRect(zdl,
          (ImVec2){thumb_x - thumb_hw, zs_cy - thumb_hh},
          (ImVec2){thumb_x + thumb_hw, zs_cy + thumb_hh},
          thumb_brd, thumb_hw * 0.5f, 0, 1.8f);

      } else {

        g_zslider_half_h = zs_half_h;
        g_zslider_left   = zs_cx - zs_half_w * 1.5f;
        g_zslider_right  = zs_cx + zs_half_w * 1.5f;
        g_zslider_top    = zs_cy - zs_half_h;
        g_zslider_bottom = zs_cy + zs_half_h;

        ImDrawList_AddRectFilled(zdl,
          (ImVec2){zs_cx - zs_half_w * 0.18f, zs_cy - zs_half_h},
          (ImVec2){zs_cx + zs_half_w * 0.18f, zs_cy + zs_half_h},
          track_col, zs_half_w * 0.18f, 0);

        float lbl_sz = zs_half_w * 1.1f;
        ImVec2 lbl_in_sz;  igCalcTextSize(&lbl_in_sz,  "+", NULL, false, -1.0f);
        ImVec2 lbl_out_sz; igCalcTextSize(&lbl_out_sz, "-", NULL, false, -1.0f);
        float lbl_sc = lbl_sz / igGetFontSize();
        ImDrawList_AddText_FontPtr(zdl, igGetFont(), lbl_sz,
          (ImVec2){zs_cx - lbl_in_sz.x  * lbl_sc * 0.5f, zs_cy + zs_half_h + 4},
          IM_COL32(255,255,255,(int)(120*zopa)), "+", NULL, 0, NULL);
        ImDrawList_AddText_FontPtr(zdl, igGetFont(), lbl_sz,
          (ImVec2){zs_cx - lbl_out_sz.x * lbl_sc * 0.5f, zs_cy - zs_half_h - lbl_sz - 4},
          IM_COL32(255,255,255,(int)(120*zopa)), "-", NULL, 0, NULL);

        float thumb_y = zs_cy + s_thumb_vis_offset;
        float thumb_h = zs_half_w * 1.4f;
        float thumb_w = zs_half_w * 1.1f;
        ImDrawList_AddRectFilled(zdl,
          (ImVec2){zs_cx - thumb_w, thumb_y - thumb_h},
          (ImVec2){zs_cx + thumb_w, thumb_y + thumb_h},
          t_fill, thumb_w * 0.5f, 0);
        ImDrawList_AddRect(zdl,
          (ImVec2){zs_cx - thumb_w, thumb_y - thumb_h},
          (ImVec2){zs_cx + thumb_w, thumb_y + thumb_h},
          thumb_brd, thumb_w * 0.5f, 0, 1.8f);
      }
    }
  }

  if (usrs->hotkeys[HOTKEY_BOT].active && usrs->bot_vis) {
    extern sbot_dbg g_bot_dbg;
    if (g_bot_dbg.active) {
      ImDrawList* bdl = igGetWindowDrawList();
      float gsc = gdata->data.gsc;
      float vx  = gdata->data.view_xx;
      float vy  = gdata->data.view_yy;
      double T  = igGetTime();
#define W2SX(wx) (mww2 + ((wx) - vx) * gsc)
#define W2SY(wy) (mhh2 + ((wy) - vy) * gsc)
#define PULSE(base, spd) ((float)((base) * (0.5 + 0.5 * sin(T * (spd)))))

      float shx = W2SX(g_bot_dbg.bx);
      float shy = W2SY(g_bot_dbg.by);

      /* 1. body segments (type=1): blue filled dots */
      for (int _i = 0; _i < g_bot_dbg.coll_n; _i++) {
        if (g_bot_dbg.coll_type[_i] != 1) continue;
        float cx = W2SX(g_bot_dbg.coll_x[_i]);
        float cy = W2SY(g_bot_dbg.coll_y[_i]);
        float cr = (g_bot_dbg.coll_r[_i] * 0.5f) * gsc;
        if (cr < 3.0f) cr = 3.0f;
        ImDrawList_AddCircleFilled(bdl, (ImVec2){cx, cy}, cr,
          IM_COL32(50, 120, 255, 128), 16);
      }

      /* 2. gradient threat lines + heatmap (head -> each coll pt) */
      for (int _i = 0; _i < g_bot_dbg.coll_n; _i++) {
        float cx  = W2SX(g_bot_dbg.coll_x[_i]);
        float cy  = W2SY(g_bot_dbg.coll_y[_i]);
        float d   = sqrtf(g_bot_dbg.coll_d2[_i]);
        float rad = g_bot_dbg.brad;
        ImU32 col;
        if (d < rad * 5.0f) {
          col = IM_COL32(255, 60,  0, 200);
          /* heatmap: concentric red glow */
          float hr = g_bot_dbg.coll_r[_i] * gsc * 3.0f;
          ImDrawList_AddCircleFilled(bdl,(ImVec2){cx,cy},hr,  IM_COL32(255,0,0,30),20);
          ImDrawList_AddCircleFilled(bdl,(ImVec2){cx,cy},hr*.6f,IM_COL32(255,80,0,50),16);
          ImDrawList_AddCircleFilled(bdl,(ImVec2){cx,cy},hr*.3f,IM_COL32(255,165,0,70),12);
        } else if (d < rad * 15.0f) {
          col = IM_COL32(255, 220, 0, 180);
          float hr = g_bot_dbg.coll_r[_i] * gsc * 2.0f;
          ImDrawList_AddCircleFilled(bdl,(ImVec2){cx,cy},hr,  IM_COL32(255,165,0,25),16);
          ImDrawList_AddCircleFilled(bdl,(ImVec2){cx,cy},hr*.5f,IM_COL32(255,220,0,40),12);
        } else {
          col = IM_COL32(50, 220, 80, 140);
        }
        ImDrawList_AddLine(bdl, (ImVec2){shx, shy}, (ImVec2){cx, cy}, col, 1.5f);
      }

      /* 3. collision point circles by type */
      for (int _i = 0; _i < g_bot_dbg.coll_n; _i++) {
        float cx = W2SX(g_bot_dbg.coll_x[_i]);
        float cy = W2SY(g_bot_dbg.coll_y[_i]);
        float cr = g_bot_dbg.coll_r[_i] * gsc;
        if (cr < 3.0f) cr = 3.0f;
        if (g_bot_dbg.coll_type[_i] == 0) {
          /* enemy head: red outline */
          ImDrawList_AddCircle(bdl, (ImVec2){cx, cy}, cr,
            IM_COL32(255, 50, 50, 220), 24, 2.0f);
        } else if (g_bot_dbg.coll_type[_i] == 2) {
          /* wall point: yellow outline */
          ImDrawList_AddCircle(bdl, (ImVec2){cx, cy}, cr,
            IM_COL32(255, 220, 0, 200), 16, 1.5f);
        }
      }

      /* 4. head detection circle (yellow, pulsing) */
      float hcx = W2SX(g_bot_dbg.head_cx);
      float hcy = W2SY(g_bot_dbg.head_cy);
      float hcr = g_bot_dbg.head_cr * gsc;
      ImDrawList_AddCircle(bdl, (ImVec2){hcx, hcy}, hcr,
        IM_COL32(255, 230, 0, (int)(PULSE(200.0f, 2.0f))), 32, 1.5f);

      /* 5. encircle detection circles */
      float er1 = g_bot_dbg.encircle_r1 * gsc;
      float er2 = g_bot_dbg.encircle_r2 * gsc;
      if (g_bot_dbg.encircle_state == 1) {
        /* being encircled by one snake: red filled */
        ImDrawList_AddCircleFilled(bdl, (ImVec2){shx, shy}, er1,
          IM_COL32(255, 50, 50, (int)(PULSE(60.0f, 3.0f))), 48);
        ImDrawList_AddCircle(bdl, (ImVec2){shx, shy}, er1,
          IM_COL32(255, 50, 50, (int)(PULSE(200.0f, 3.0f))), 48, 2.0f);
      } else if (g_bot_dbg.encircle_state == 2) {
        /* all-around threat: yellow filled */
        ImDrawList_AddCircleFilled(bdl, (ImVec2){shx, shy}, er2,
          IM_COL32(255, 180, 0, (int)(PULSE(50.0f, 2.0f))), 48);
        ImDrawList_AddCircle(bdl, (ImVec2){shx, shy}, er2,
          IM_COL32(255, 180, 0, (int)(PULSE(180.0f, 2.0f))), 48, 2.0f);
      } else {
        /* no threat: quiet blue pulsing outline */
        ImDrawList_AddCircle(bdl, (ImVec2){shx, shy}, er1,
          IM_COL32(80, 180, 255, (int)(PULSE(120.0f, 1.0f))), 48, 1.5f);
      }

      /* 6. avoidance lines (orange forward + red to intersect) */
      if (g_bot_dbg.avoid_active) {
        float fwdx = W2SX(g_bot_dbg.avoid_fwd_x);
        float fwdy = W2SY(g_bot_dbg.avoid_fwd_y);
        float aix  = W2SX(g_bot_dbg.avoid_ix);
        float aiy  = W2SY(g_bot_dbg.avoid_iy);
        ImDrawList_AddLine(bdl, (ImVec2){shx, shy}, (ImVec2){fwdx, fwdy},
          IM_COL32(255, 140, 0, 220), 2.0f);
        ImDrawList_AddLine(bdl, (ImVec2){shx, shy}, (ImVec2){aix, aiy},
          IM_COL32(255, 40, 40, 220), 2.0f);
        ImDrawList_AddCircleFilled(bdl, (ImVec2){aix, aiy}, 6.0f,
          IM_COL32(255, 40, 40, 255), 12);
      }

      /* 7. food target: green crosshair + dim line */
      if (g_bot_dbg.has_food) {
        float fx  = W2SX(g_bot_dbg.food_x);
        float fy  = W2SY(g_bot_dbg.food_y);
        float fcs = 12.0f;
        ImDrawList_AddLine(bdl,(ImVec2){shx,shy},(ImVec2){fx,fy},
          IM_COL32(50,220,80,80),1.0f);
        ImDrawList_AddCircleFilled(bdl,(ImVec2){fx,fy},5.0f,
          IM_COL32(50,255,100,230),12);
        ImDrawList_AddLine(bdl,(ImVec2){fx-fcs,fy},(ImVec2){fx+fcs,fy},
          IM_COL32(50,255,100,230),1.8f);
        ImDrawList_AddLine(bdl,(ImVec2){fx,fy-fcs},(ImVec2){fx,fy+fcs},
          IM_COL32(50,255,100,230),1.8f);
      }

      /* 8. goal: magenta crosshair + line from head */
      float gx  = W2SX(g_bot_dbg.goal_x);
      float gy  = W2SY(g_bot_dbg.goal_y);
      float gcs = 14.0f;
      ImDrawList_AddLine(bdl,(ImVec2){shx,shy},(ImVec2){gx,gy},
        IM_COL32(255,80,255,160),1.5f);
      ImDrawList_AddLine(bdl,(ImVec2){gx-gcs,gy},(ImVec2){gx+gcs,gy},
        IM_COL32(255,80,255,255),2.0f);
      ImDrawList_AddLine(bdl,(ImVec2){gx,gy-gcs},(ImVec2){gx,gy+gcs},
        IM_COL32(255,80,255,255),2.0f);
      ImDrawList_AddCircle(bdl,(ImVec2){gx,gy},6.0f,
        IM_COL32(255,80,255,255),12,2.0f);

      /* 9. stage badge */
      const char* stage_lbl = g_bot_dbg.stage == 2 ? "BOT: COIL"
                            : g_bot_dbg.stage == 1 ? "BOT: ENCIRCLE"
                            :                        "BOT: AVOID";
      ImU32 stage_col = g_bot_dbg.stage == 2 ? IM_COL32(255,160, 40,255)
                      : g_bot_dbg.stage == 1 ? IM_COL32(255, 80, 80,255)
                      :                        IM_COL32( 80,200,255,255);
      float badge_x = mww2 * 0.06f;
      float badge_y = mhh2 * 0.08f;
      ImDrawList_AddRectFilled(bdl,
        (ImVec2){badge_x - 6, badge_y - 4},
        (ImVec2){badge_x + 170, badge_y + 46},
        IM_COL32(0,0,0,160), 4.0f, 0);
      ImDrawList_AddText_FontPtr(bdl, NULL, 18.0f,
        (ImVec2){badge_x, badge_y}, stage_col, stage_lbl, NULL, 0, NULL);
      const char* accel_lbl = g_bot_dbg.accel ? "  BOOST" : "  coast";
      ImU32 accel_col = g_bot_dbg.accel ? IM_COL32(255,220,50,255)
                                         : IM_COL32(160,160,160,180);
      ImDrawList_AddText_FontPtr(bdl, NULL, 16.0f,
        (ImVec2){badge_x, badge_y + 24}, accel_col, accel_lbl, NULL, 0, NULL);

#undef PULSE
#undef W2SX
#undef W2SY
    }
  }
#endif
}