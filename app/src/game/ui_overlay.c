#ifdef ANDROID
#include "../android_glfw_shim.h"
/* IM_COL32 lives in the C++ imgui.h only — define it here for C files */
#ifndef IM_COL32
#define IM_COL32(R,G,B,A) \
  (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0))
#endif
#endif
#include "ui_overlay.h"

#include <math.h>
#include "../user.h"
#include "user_settings.h"
#include "../ui/chat.h"
#include "../network/ntl_client.h"

static const char* clean_ntl_nickname(const char* raw_nick) {
  if (!raw_nick) return "";
  if (strlen(raw_nick) >= 8) {
    bool is_hex = true;
    for (int i = 0; i < 8; i++) {
      char c = raw_nick[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
        is_hex = false;
        break;
      }
    }
    if (is_hex) {
      return raw_nick + 8;
    }
  }
  return raw_nick;
}

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
        /* On Android ms->pos is always (0,0) — use touch cursor instead */
#ifdef ANDROID
        float laser_tx = gdata->touch_ctrl.tp_cursor_x;
        float laser_ty = gdata->touch_ctrl.tp_cursor_y;
        /* If cursor not yet initialized, fall back to screen centre */
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
    // igPushFont(
    //     usr->imgui_data.mono_font_bold[usrs->stats_font_size],
    //     usr->imgui_data.mono_font_bold[usrs->stats_font_size]->LegacySize);
    igTextColored((ImVec4){1, 1, 1, 0.7}, "%d", gdata->data.rank);
    // igPopFont();
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
            itcolor.w = 0.6f;  // .7f * (.3f + .7f * (1 - (1 + row) / 10.0f));
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

    // Draw team player dots on the minimap
    float R_mm = usrs->minimap_size * 0.5f;
    float mx_center = usr->r->global.minimap_circ[0] + R_mm;
    float my_center = usr->r->global.minimap_circ[1] + R_mm;
    int mm_ntl_count = 0;
    ntl_player* mm_ntl_players = ntl_get_players(&mm_ntl_count);
    if (mm_ntl_count > 0 && gdata->data.flux_grd > 0) {
      ImDrawList* draw_list = igGetWindowDrawList();
      for (int i = 0; i < mm_ntl_count; i++) {
        if (strcmp(mm_ntl_players[i].nickname, "00000000") == 0) continue;
        if (strcmp(mm_ntl_players[i].server, usrs->ipv4) == 0) {
          float tx = atof(mm_ntl_players[i].valx);
          float ty = atof(mm_ntl_players[i].valy);
          if (tx > 0 && ty > 0) {
            /* Same normalization the engine already uses for the local
               player's own minimap arrow (see px/py above), so teammate
               dots land in the same coordinate space. */
            float rx = (tx - gdata->data.grd) / gdata->data.flux_grd;
            float ry = (ty - gdata->data.grd) / gdata->data.flux_grd;

            float dist = sqrtf(rx * rx + ry * ry);
            if (dist > 1.0f) {
              rx /= dist;
              ry /= dist;
            }

            float sx = mx_center + rx * R_mm;
            float sy = my_center + ry * R_mm;

            ImU32 dot_color = igColorConvertFloat4ToU32((ImVec4){0.0f, 1.0f, 0.5f, 1.0f});
            ImU32 border_color = igColorConvertFloat4ToU32((ImVec4){0.0f, 0.0f, 0.0f, 0.8f});

            ImDrawList_AddCircleFilled(draw_list, (ImVec2){sx, sy}, 4.0f, dot_color, 12);
            ImDrawList_AddCircle(draw_list, (ImVec2){sx, sy}, 4.0f, border_color, 12, 1.0f);

            char clean_name[16];
            strncpy(clean_name, clean_ntl_nickname(mm_ntl_players[i].nickname), 15);
            clean_name[15] = '\0';

            igPushFont(usr->imgui_data.mono_font[FONT_SIZE_TINY],
                       usr->imgui_data.mono_font[FONT_SIZE_TINY]->LegacySize);
            ImVec2 txtsz;
            igCalcTextSize(&txtsz, clean_name, NULL, false, -1);

            ImDrawList_AddText_Vec2(draw_list, (ImVec2){sx - txtsz.x * 0.5f, sy - txtsz.y - 4},
                                    igColorConvertFloat4ToU32((ImVec4){1.0f, 1.0f, 1.0f, 0.8f}),
                                    clean_name, NULL);
            igPopFont();
          }
        }
      }
    }

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
  /* Signal to twindow_android.c that the overlay is active this frame */
  { extern bool g_overlay_drawn_this_frame; g_overlay_drawn_this_frame = true; }

  /* ============================================================
   * TOUCH CONTROLS OVERLAY
   * Joystick mode  – virtual joystick ring (bottom left or right)
   * Trackpad mode  – NO placeholder ring; only cursor dot shown
   * Boost          – circle (bottom right or left, based on swap)
   * Swap button    – small fade button at top centre
   * Active ONLY during gameplay (follow_view == true)
   * ============================================================ */

  /* Keep global swap-state in sync */
  extern bool g_ctrl_swap_sides;
  g_ctrl_swap_sides = usrs->ctrl_swap_sides;

  if (gdata->data.follow_view) {
    ImDrawList* dl  = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
    float sw        = (float)ctx->size[0];
    float sh        = (float)ctx->size[1];
    float margin    = sw * 0.025f;

    bool  boost_on  = env->wnd->touch.boost_down;
    bool  swapped   = usrs->ctrl_swap_sides;

    /* ── Zoom slider globals (read by twindow_android + tentry) ── */
    extern float g_zslider_left, g_zslider_top, g_zslider_right, g_zslider_bottom;
    extern float g_zslider_half_h;
    extern float g_zoom_sensitivity;
    g_zoom_sensitivity = usrs->zoom_sensitivity;

    /* ── compute layout positions ──────────────────────────────── */
    /* Boost - custom or auto from swap */
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

    /* Joystick/trackpad ring - custom or auto */
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

    /* Expose button positions for twindow_android.c circle-only detection */
    { extern float g_boost_cx,g_boost_cy,g_boost_r,g_joy_cx,g_joy_cy,g_joy_r;
      extern bool  g_is_trackpad_mode, g_panel_open;
      g_boost_cx = bcx; g_boost_cy = bcy; g_boost_r = br;
      g_joy_cx   = jcx; g_joy_cy   = jcy; g_joy_r   = jr;
      g_is_trackpad_mode = usrs->ctrl_mode_trackpad;
      (void)g_panel_open; /* read in game move suppression below */ }

    /* ── JOYSTICK (only in joystick mode) ──────────────────────── */
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
      if (joy_on) {
        float dx   = env->wnd->touch.x - gdata->touch_ctrl.joy_anchor_x;
        float dy   = env->wnd->touch.y - gdata->touch_ctrl.joy_anchor_y;
        float dist = sqrtf(dx * dx + dy * dy);
        float cap  = jr * 0.68f;
        float sc   = (dist > cap && dist > 0.001f) ? cap / dist : 1.0f;
        jtx = jcx + dx * sc;
        jty = jcy + dy * sc;
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
      /* ── TRACKPAD MODE: CSS-style polygon arrow cursor ─────────── */
      if (gdata->touch_ctrl.tp_tracking && gdata->touch_ctrl.tp_visible) {
        float acx = gdata->touch_ctrl.tp_cursor_x;
        float acy = gdata->touch_ctrl.tp_cursor_y;

        /* Direction toward screen centre */
        float dir_angle = atan2f(sh * 0.5f - acy, sw * 0.5f - acx);
        /* Tip points AWAY from centre (outward toward the touch position).
           The polygon tip is on the left (-x), rotating by dir_angle+pi made it
           point toward centre; now rotate by dir_angle to point outward. */
        float rot  = dir_angle;
        float cs   = cosf(rot);
        float sn_v = sinf(rot);

        /* Arrow dimensions (matches CSS 300×180 aspect ratio) */
        float aw = sh * 0.11f;    /* full width  */
        float ah = sh * 0.066f;   /* full height */

        /* Rotate local point (px,py) around cursor (acx,acy) */
        #define ARPT(px, py) \
          (ImVec2){ acx + (px)*cs - (py)*sn_v, \
                    acy + (px)*sn_v + (py)*cs }

        ImU32 fill_col   = IM_COL32(198, 67, 79, 230);
        ImU32 border_col = IM_COL32(139, 58, 71, 255);

        /* Body rectangle: x [-0.10..+0.50]*aw, y [-0.25..+0.25]*ah */
        ImVec2 body[4] = {
          ARPT(-0.10f*aw, -0.25f*ah),
          ARPT( 0.50f*aw, -0.25f*ah),
          ARPT( 0.50f*aw,  0.25f*ah),
          ARPT(-0.10f*aw,  0.25f*ah),
        };
        ImDrawList_AddConvexPolyFilled(dl, body, 4, fill_col);

        /* Top arrowhead wing */
        ImDrawList_AddTriangleFilled(dl,
          ARPT(-0.10f*aw, -0.50f*ah),
          ARPT(-0.10f*aw, -0.25f*ah),
          ARPT(-0.50f*aw,  0.00f   ),
          fill_col);

        /* Bottom arrowhead wing */
        ImDrawList_AddTriangleFilled(dl,
          ARPT(-0.10f*aw,  0.25f*ah),
          ARPT(-0.10f*aw,  0.50f*ah),
          ARPT(-0.50f*aw,  0.00f   ),
          fill_col);

        /* Border outline – full 7-point polygon, closed */
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

        #undef ARPT
      }
    }

    /* ── BOOST BUTTON ───────────────────────────────────────────── */
    {
      float bo = usrs->boost_opacity;
      ImDrawList_AddCircleFilled(dl,
        (ImVec2){bcx, bcy}, br,
        boost_on ? IM_COL32(255,80,55,(int)(210*bo))
                 : IM_COL32(210,55,35,(int)(85*bo)), 48);
      ImDrawList_AddCircle(dl,
        (ImVec2){bcx, bcy}, br,
        boost_on ? IM_COL32(255,140,110,(int)(230*bo))
                 : IM_COL32(210,90,65,(int)(130*bo)),
        48, 3.0f);
    }

    float bfont = br * 0.40f;
    ImVec2 bt_sz; igCalcTextSize(&bt_sz, "BOOST", NULL, false, -1.0f);
    float bt_scale = bfont / igGetFontSize();
    ImDrawList_AddText_FontPtr(dl, igGetFont(), bfont,
      (ImVec2){bcx - bt_sz.x * bt_scale * 0.5f, bcy - bfont * 0.5f},
      IM_COL32(255, 255, 255, boost_on ? 255 : 200),
      "BOOST", NULL, 0.0f, NULL);

  }

  /* ── ON-SCREEN HOTKEY BUTTONS ──────────────────────────────────────
     Each enabled hotkey shows as a small tap button along the top.
     Tapping toggles the hotkey active state.                            */
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
      /* Tap to toggle */
      ImGuiIO* hkio = igGetIO_Nil();
      if (hkio && igIsMouseClicked_Bool(0, false)) {
        float mx = hkio->MousePos.x, my = hkio->MousePos.y;
        if (mx >= bx && mx <= bx+btn_w && my >= by && my <= by+btn_h)
          usrs->hotkeys[hi].active = !usrs->hotkeys[hi].active;
      }
    }
  }

    /* ═══════════════════════════════════════════════════════════════════
     ZOOM SLIDER  –  always visible during PLAYING, right side (default)
     3rd-finger drag:  up = zoom out, down = zoom in
     Thumb animates back to centre when released.
     ═══════════════════════════════════════════════════════════════════ */
  if (gdata->data.follow_view) {
    /* Expose globals for twindow_android.c touch routing */
    extern float g_zslider_left, g_zslider_top, g_zslider_right, g_zslider_bottom;
    extern float g_zslider_half_h;

    ImDrawList* zdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
    float sw2 = (float)ctx->size[0];
    float sh2 = (float)ctx->size[1];

    /* Slider geometry from settings */
    float zs_half_h = sh2 * usrs->zslider_rel_h;
    float zs_half_w = sh2 * 0.022f;             /* thin bar */
    float zs_cx     = sw2 * usrs->zslider_rel_x;
    float zs_cy     = sh2 * usrs->zslider_rel_y;

    /* Expose for touch routing */
    g_zslider_half_h = zs_half_h;
    g_zslider_left   = zs_cx - zs_half_w * 1.5f;
    g_zslider_right  = zs_cx + zs_half_w * 1.5f;
    g_zslider_top    = zs_cy - zs_half_h;
    g_zslider_bottom = zs_cy + zs_half_h;

    float zopa = usrs->zslider_opacity;
    ImU32 track_col = IM_COL32(255, 255, 255, (int)(50  * zopa));
    ImU32 track_brd = IM_COL32(255, 255, 255, (int)(90  * zopa));
    ImU32 thumb_col = IM_COL32(255, 255, 255, (int)(180 * zopa));
    ImU32 thumb_brd = IM_COL32(255, 255, 255, (int)(230 * zopa));

    /* Track bar */
    ImDrawList_AddRectFilled(zdl,
      (ImVec2){zs_cx - zs_half_w * 0.18f, zs_cy - zs_half_h},
      (ImVec2){zs_cx + zs_half_w * 0.18f, zs_cy + zs_half_h},
      track_col, zs_half_w * 0.18f, 0);

    /* Zoom labels */
    float lbl_sz = zs_half_w * 1.1f;
    ImVec2 lbl_in_sz; igCalcTextSize(&lbl_in_sz, "+", NULL, false, -1.0f);
    float lbl_sc = lbl_sz / igGetFontSize();
    ImDrawList_AddText_FontPtr(zdl, igGetFont(), lbl_sz,
      (ImVec2){zs_cx - lbl_in_sz.x * lbl_sc * 0.5f, zs_cy + zs_half_h + 4},
      IM_COL32(255,255,255,(int)(120*zopa)), "+", NULL, 0, NULL);
    ImVec2 lbl_out_sz; igCalcTextSize(&lbl_out_sz, "-", NULL, false, -1.0f);
    ImDrawList_AddText_FontPtr(zdl, igGetFont(), lbl_sz,
      (ImVec2){zs_cx - lbl_out_sz.x * lbl_sc * 0.5f, zs_cy - zs_half_h - lbl_sz - 4},
      IM_COL32(255,255,255,(int)(120*zopa)), "-", NULL, 0, NULL);

    /* Thumb: animate toward current offset position */
    static float s_thumb_vis_offset = 0.0f;
    float target_offset = env->wnd->touch.zslider_offset;
    s_thumb_vis_offset += (target_offset - s_thumb_vis_offset) * 0.25f;
    float thumb_y = zs_cy + s_thumb_vis_offset;
    float thumb_h = zs_half_w * 1.4f;
    float thumb_w = zs_half_w * 1.1f;
    bool  touching = (env->wnd->touch.zslider_ptr_id != -1);

    /* ── Direct zoom update ─────────────────────────────────────────
       Apply zoom straight to ms_zoom here — this is far more reliable
       than the dwheel chain which depends on call order and connection
       state.  ui_overlay runs inside game_loop so gdata is accessible. */
    if (touching && target_offset != 0.0f) {
      float half = zs_half_h > 1.0f ? zs_half_h : 1.0f;
      float norm = target_offset / half;
      if (norm >  1.0f) norm =  1.0f;
      if (norm < -1.0f) norm = -1.0f;
      /* norm > 0 → thumb below centre → zoom IN */
      gdata->data.ms_zoom *= expf(norm * 4.0f
                                  * usrs->zoom_sensitivity
                                  * usrs->zoom_step);
      if (gdata->data.ms_zoom > MAX_ZOOM_IN)  gdata->data.ms_zoom = MAX_ZOOM_IN;
      if (gdata->data.ms_zoom < MAX_ZOOM_OUT) gdata->data.ms_zoom = MAX_ZOOM_OUT;
    }
    ImU32 t_fill   = touching ? IM_COL32(180,220,255,(int)(220*zopa))
                               : IM_COL32(255,255,255,(int)(160*zopa));
    ImDrawList_AddRectFilled(zdl,
      (ImVec2){zs_cx - thumb_w, thumb_y - thumb_h},
      (ImVec2){zs_cx + thumb_w, thumb_y + thumb_h},
      t_fill, thumb_w * 0.5f, 0);
    ImDrawList_AddRect(zdl,
      (ImVec2){zs_cx - thumb_w, thumb_y - thumb_h},
      (ImVec2){zs_cx + thumb_w, thumb_y + thumb_h},
      thumb_brd, thumb_w * 0.5f, 0, 1.8f);
  }
#endif /* ANDROID */
}

void ui_online_players_hud(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;
  if (!usrs->show_online_players_hud) return;

  static float last_scr_w = 0;
  static float last_scr_h = 0;
  static float ratio_x = -1;
  static float ratio_y = -1;

  float scr_w = env->ctx->size[0];
  float scr_h = env->ctx->size[1];

  if (last_scr_w > 0 && last_scr_h > 0 && (scr_w != last_scr_w || scr_h != last_scr_h)) {
    if (ratio_x >= 0 && ratio_y >= 0) {
      ImVec2 new_pos = { ratio_x * scr_w, ratio_y * scr_h };
      igSetWindowPos_Str("Online Players##ntl_hud", new_pos, ImGuiCond_Always);
    }
  }

  bool settings_open = gdata->curr_screen == SETTINGS;

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  if (!settings_open) {
    flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground;
  }

  // Set default window pos/size on first use
  igSetNextWindowPos((ImVec2){20.0f, 150.0f}, ImGuiCond_FirstUseEver, (ImVec2){0, 0});
  igSetNextWindowSize((ImVec2){250.0f, 150.0f}, ImGuiCond_FirstUseEver);
  igSetNextWindowBgAlpha(0.6f);

  if (igBegin("Online Players##ntl_hud", &usrs->show_online_players_hud, flags)) {
    igPushFont(usr->imgui_data.mono_font[FONT_SIZE_TINY],
               usr->imgui_data.mono_font[FONT_SIZE_TINY]->LegacySize);

    int team_count = 0;
    ntl_player* players = ntl_get_players(&team_count);

    igTextColored((ImVec4){1.0f, 1.0f, 1.0f, 0.8f}, "Online players (key owner, nick, srv, ver):");

    for (int i = 0; i < team_count; i++) {
      if (strcmp(players[i].nickname, "00000000") == 0) continue;
      // Key owner in red/orange:
      igTextColored((ImVec4){0.85f, 0.2f, 0.2f, 1.0f}, "%s", players[i].tlm[0] ? players[i].tlm : "unknown");
      igSameLine(0, 4);
      // Cleaned nickname in cyan:
      igTextColored((ImVec4){0.2f, 0.8f, 0.8f, 1.0f}, "%s", clean_ntl_nickname(players[i].nickname));
      igSameLine(0, 4);
      // Server in cyan/gray:
      igTextColored((ImVec4){0.2f, 0.8f, 0.8f, 0.8f}, "%s", players[i].server);
      igSameLine(0, 4);
      // Version in cyan/gray:
      igTextColored((ImVec4){0.2f, 0.8f, 0.8f, 0.8f}, "v%s", players[i].ver);
    }

    if (team_count == 0) {
      igTextColored((ImVec4){0.6f, 0.6f, 0.6f, 1.0f}, "No team players online");
    }

    ImVec2 pos;
    igGetWindowPos(&pos);
    ratio_x = pos.x / scr_w;
    ratio_y = pos.y / scr_h;

    igPopFont();
  }
  igEnd();
  last_scr_w = scr_w;
  last_scr_h = scr_h;
}

void ui_player_details_hud(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;
  if (!usrs->show_player_details_hud) return;

  static float last_scr_w = 0;
  static float last_scr_h = 0;
  static float ratio_x = -1;
  static float ratio_y = -1;

  float scr_w = env->ctx->size[0];
  float scr_h = env->ctx->size[1];

  if (last_scr_w > 0 && last_scr_h > 0 && (scr_w != last_scr_w || scr_h != last_scr_h)) {
    if (ratio_x >= 0 && ratio_y >= 0) {
      ImVec2 new_pos = { ratio_x * scr_w, ratio_y * scr_h };
      igSetWindowPos_Str("Players List##ntl_hud", new_pos, ImGuiCond_Always);
    }
  }

  bool settings_open = gdata->curr_screen == SETTINGS;

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  if (!settings_open) {
    flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground;
  }

  // Set default window pos/size on first use
  igSetNextWindowPos((ImVec2){env->ctx->size[0] - 320.0f, 150.0f}, ImGuiCond_FirstUseEver, (ImVec2){0, 0});
  igSetNextWindowSize((ImVec2){300.0f, 200.0f}, ImGuiCond_FirstUseEver);
  igSetNextWindowBgAlpha(0.6f);

  if (igBegin("Players List##ntl_hud", &usrs->show_player_details_hud, flags)) {
    igPushFont(usr->imgui_data.mono_font[FONT_SIZE_TINY],
               usr->imgui_data.mono_font[FONT_SIZE_TINY]->LegacySize);

    int team_count = 0;
    ntl_player* players = ntl_get_players(&team_count);

    igText("Players list:");
    igText("------------------------------");

    for (int i = 0; i < team_count; i++) {
      if (strcmp(players[i].nickname, "00000000") == 0) continue;
      // Determine if local player
      bool is_me = (strcmp(clean_ntl_nickname(players[i].nickname), usrs->nickname) == 0) ||
                   (strlen(players[i].nickname) >= 8 && strncmp(players[i].nickname, usrs->ntl_user_id, 8) == 0);

      ImVec4 name_col = is_me ? (ImVec4){0.2f, 0.8f, 0.8f, 1.0f} : (ImVec4){0.85f, 0.2f, 0.2f, 1.0f};
      ImVec4 stats_col = is_me ? (ImVec4){0.2f, 0.8f, 0.8f, 1.0f} : (ImVec4){0.7f, 0.7f, 0.7f, 1.0f};

      // Diamond symbol or bot/sos marker based on status
      const char* sym = "\u25c6";
      if (players[i].is_bot) sym = "[Bot]";
      else if (players[i].is_sos) sym = "[SOS]";

      igTextColored(name_col, "%s %s : : %s", sym, players[i].tlm[0] ? players[i].tlm : "unknown", players[i].score);

      // Stats line:
      igTextColored(stats_col, "    %s", players[i].dt);

      // Server line with "Play" button
      bool on_menu = (strcmp(players[i].server, "_GAME_MENU_") == 0);
      if (on_menu) {
        igTextColored(stats_col, "    _GAME_MENU_");
      } else {
        igIndent(16.0f);

        // Draw the red "Play" button
        igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.75f, 0.15f, 0.15f, 1.0f});
        igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.90f, 0.20f, 0.20f, 1.0f});
        igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.60f, 0.10f, 0.10f, 1.0f});
        igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 4.0f);
        igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){6, 1});

        char btn_id[64];
        sprintf(btn_id, "Play##play_%d", i);

        if (igButton(btn_id, (ImVec2){34, 16})) {
          strcpy(usrs->ipv4, players[i].server);
          save_user_settings(usrs);

          if (gdata->connection) {
            gdata->connection->is_closing = true;
          }
          gdata->restart_req = true;
        }

        igPopStyleVar(2);
        igPopStyleColor(3);

        igSameLine(0, 4);
        igTextColored(stats_col, "%s", players[i].server);
        igUnindent(16.0f);
      }
    }

    if (team_count == 0) {
      igTextColored((ImVec4){0.6f, 0.6f, 0.6f, 1.0f}, "No team players online");
    }

    ImVec2 pos;
    igGetWindowPos(&pos);
    ratio_x = pos.x / scr_w;
    ratio_y = pos.y / scr_h;

    igPopFont();
  }
  igEnd();
  last_scr_w = scr_w;
  last_scr_h = scr_h;
}
void ui_ntl_panel(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;
  if (!gdata->show_ntl_panel) return;

  float panel_w = env->ctx->size[0] * 0.5f;
  float panel_h = (float)env->ctx->size[1];

  igSetNextWindowPos((ImVec2){0, 0}, ImGuiCond_Always, (ImVec2){0, 0});
  igSetNextWindowSize((ImVec2){panel_w, panel_h}, ImGuiCond_Always);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoCollapse;

  /* No p_open here on purpose — no cross in the title bar. Closing happens
     via the OK/Reset buttons below, or by tapping outside the panel. */
  if (igBegin("NTL##ntl_panel", NULL, flags)) {
    igSeparatorText("NTL Mod & Team Settings");

    igCheckbox("Show chat", &usrs->show_chat_hud);
    igCheckbox("Show online players list", &usrs->show_online_players_hud);
    igCheckbox("Show player details list", &usrs->show_player_details_hud);

    igSpacing();
    igSeparator();
    igSpacing();
    igText("Active team:");

    static char edit_team_id[33] = {0};
    static char edit_auth_key[65] = {0};
    static char edit_profile_name[33] = {0};
    static int loaded_for_idx = -2; /* -2 = never loaded yet */

    const char* active_label = "None";
    if (usrs->ntl_active_team_idx >= 0 &&
        usrs->ntl_active_team_idx < usrs->ntl_team_count) {
      active_label = usrs->ntl_teams[usrs->ntl_active_team_idx].name;
    }

    igSetNextItemWidth(-1);
    if (igBeginCombo("##ntl_active_team", active_label, ImGuiComboFlags_None)) {
      if (igSelectable_Bool("None", usrs->ntl_active_team_idx == -1,
                            ImGuiSelectableFlags_None, (ImVec2){})) {
        usrs->ntl_active_team_idx = -1;
      }
      for (int i = 0; i < usrs->ntl_team_count; i++) {
        igPushID_Int(i);
        if (igSelectable_Bool(usrs->ntl_teams[i].name,
                              usrs->ntl_active_team_idx == i,
                              ImGuiSelectableFlags_None, (ImVec2){})) {
          usrs->ntl_active_team_idx = i;
        }
        igPopID();
      }
      igEndCombo();
    }

    /* Whenever the active selection changes, refresh the edit fields from
       that profile's saved values (or clear them for "None" / a brand new
       profile that hasn't been added yet). */
    if (loaded_for_idx != usrs->ntl_active_team_idx) {
      loaded_for_idx = usrs->ntl_active_team_idx;
      if (loaded_for_idx >= 0 && loaded_for_idx < usrs->ntl_team_count) {
        strcpy(edit_profile_name, usrs->ntl_teams[loaded_for_idx].name);
        strcpy(edit_team_id, usrs->ntl_teams[loaded_for_idx].team_id);
        strcpy(edit_auth_key, usrs->ntl_teams[loaded_for_idx].auth_key);
      } else {
        edit_profile_name[0] = '\0';
        edit_team_id[0] = '\0';
        edit_auth_key[0] = '\0';
      }
    }

    igSpacing();
    igText("Profile name");
    igSetNextItemWidth(-1);
    igInputText("##ntl_profile_name", edit_profile_name,
               sizeof(edit_profile_name), ImGuiInputTextFlags_None, NULL, NULL);
    igText("Team ID");
    igSetNextItemWidth(-1);
    igInputText("##ntl_team_id", edit_team_id, sizeof(edit_team_id),
               ImGuiInputTextFlags_None, NULL, NULL);
    igText("Auth key");
    igSetNextItemWidth(-1);
    igInputText("##ntl_auth_key", edit_auth_key, sizeof(edit_auth_key),
               ImGuiInputTextFlags_Password, NULL, NULL);

    igSpacing();
    bool can_save = edit_profile_name[0] != '\0' && edit_team_id[0] != '\0' &&
                    edit_auth_key[0] != '\0';
    igBeginDisabled(!can_save);
    if (igButton("Save profile", (ImVec2){-1, 0})) {
      int target_idx = usrs->ntl_active_team_idx;
      if (target_idx < 0 || target_idx >= usrs->ntl_team_count) {
        if (usrs->ntl_team_count < 12) {
          target_idx = usrs->ntl_team_count;
          usrs->ntl_team_count++;
        } else {
          target_idx = -1; /* full — can't add another */
        }
      }
      if (target_idx >= 0) {
        strcpy(usrs->ntl_teams[target_idx].name, edit_profile_name);
        strcpy(usrs->ntl_teams[target_idx].team_id, edit_team_id);
        strcpy(usrs->ntl_teams[target_idx].auth_key, edit_auth_key);
        usrs->ntl_active_team_idx = target_idx;
        loaded_for_idx = target_idx;
        save_user_settings(usrs);
      }
    }
    igEndDisabled();

    igBeginDisabled(usrs->ntl_active_team_idx < 0);
    if (igButton("Delete profile", (ImVec2){-1, 0})) {
      int del_idx = usrs->ntl_active_team_idx;
      if (del_idx >= 0 && del_idx < usrs->ntl_team_count) {
        for (int i = del_idx; i < usrs->ntl_team_count - 1; i++) {
          usrs->ntl_teams[i] = usrs->ntl_teams[i + 1];
        }
        usrs->ntl_team_count--;
        usrs->ntl_active_team_idx = -1;
        loaded_for_idx = -2;
        save_user_settings(usrs);
      }
    }
    igEndDisabled();

    igSpacing();
    igSeparator();
    igSpacing();

    float half_w = (panel_w - igGetStyle()->WindowPadding.x * 2 -
                    igGetStyle()->ItemSpacing.x) * 0.5f;
    if (igButton("Reset", (ImVec2){half_w, 0})) {
      /* Discard unsaved edits — forces the reload-from-saved-profile check
         above to refresh the fields on the next frame. */
      loaded_for_idx = -2;
    }
    igSameLine(0, -1);
    if (igButton("OK", (ImVec2){half_w, 0})) {
      gdata->show_ntl_panel = false;
    }
  }
  igEnd();

  /* Tap anywhere outside the panel to close it too — but not while a combo
     dropdown or other popup spawned from inside it is open, since that
     popup's own contents can render past the panel's right/bottom edge. */
  if (!igIsPopupOpen_Str("", ImGuiPopupFlags_AnyPopupId) &&
      igIsMouseClicked_Bool(ImGuiMouseButton_Left, false)) {
    ImVec2 mp;
    igGetMousePos(&mp);
    if (mp.x < 0 || mp.y < 0 || mp.x > panel_w || mp.y > panel_h) {
      gdata->show_ntl_panel = false;
    }
  }
}
