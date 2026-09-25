#include "redraw.h"

#include "../user.h"
#include "ntl_team.h"
#include "ntl_tags.h"
#include "vlither_tags.h"

extern bool g_ntl_skin_peek_active;

static bool redraw_snake_is_teammate(const snake *o,
                                     const user_settings *usrs) {
  if (!o || !usrs) return false;
  if (vlither_chat_is_snake_player(o->id, usrs->server_address)) return true;
  if (ntl_team_is_snake_teammate(o->ntl_id, usrs->server_address)) return true;
  return ntl_team_leaderboard_status(o->nk, usrs->server_address) > 0;
}

void lerp_minimap_float(float* dst, const uint8_t* src, int mmsz, float alpha) {
  int stride = MAX_MINIMAP_SIZE;

  for (int y = 0; y < mmsz; y++) {
    for (int x = 0; x < mmsz; x++) {
      int i = y * stride + x;
      float target = (float)src[i];
      dst[i] += alpha * (target - dst[i]);
    }
  }
}

void minimap_float_to_u8(const float* src, uint8_t* dst, int mmsz) {
  int stride = MAX_MINIMAP_SIZE;

  for (int y = 0; y < mmsz; y++) {
    for (int x = 0; x < mmsz; x++) {
      int i = y * stride + x;
      float v = src[i];
      dst[i] = (uint8_t)(v + 0.5f);
    }
  }
}

int arp(snake* o, int q, float xx, float yy) {
  if (q < tdarray_length(o->gptz)) {
    int gpo_i = q;
    o->gptz[gpo_i].xx = xx;
    o->gptz[gpo_i].yy = yy;
    return gpo_i;
  } else {
    gpt gpo = {.xx = xx, .yy = yy};
    tdarray_push(&o->gptz, &gpo);
    int gptz_len = tdarray_length(o->gptz);
    return gptz_len - 1;
  }
}

void redraw(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  float lvx = gdata->data.view_xx;
  float lvy = gdata->data.view_xx;

  float mww2 = ctx->size[0] / 2.0f;
  float mhh2 = ctx->size[1] / 2.0f;

  float mwwp50 = ctx->size[0] + 50;
  float mhhp50 = ctx->size[1] + 50;

  int mode_index = usrs->hotkeys[HOTKEY_ASSIST].active ? 1 : 0;
  gameplay_mode* mode = usrs->modes + mode_index;
  bool render_shadows = mode->show_shadows && !usrs->performance_mode;
  bool render_food_glow = usrs->food_glow[mode_index] &&
                          !usrs->performance_mode;

  if (!gdata->data.dead) {
    if (gdata->data.fvtg > 0) {
      gdata->data.fvtg--;
      gdata->data.fvx = gdata->data.fvxs[gdata->data.fvpos];
      gdata->data.fvy = gdata->data.fvys[gdata->data.fvpos];
      gdata->data.fvxs[gdata->data.fvpos] = 0;
      gdata->data.fvys[gdata->data.fvpos] = 0;
      gdata->data.fvpos++;
      if (gdata->data.fvpos >= GD_VFC) gdata->data.fvpos = 0;
    }

    if (gdata->data.follow_view) {
      int snakes_len = tdarray_length(gdata->data.snakes);
      snake* me = gdata->data.snakes + (snakes_len - 1);
      gdata->data.view_xx = me->xx + me->fx + gdata->data.fvx;
      gdata->data.view_yy = me->yy + me->fy + gdata->data.fvy;
    }

    gdata->data.bpx1 = gdata->data.view_xx - (mww2 / gdata->data.gsc + 84);
    gdata->data.bpy1 = gdata->data.view_yy - (mhh2 / gdata->data.gsc + 84);
    gdata->data.bpx2 = gdata->data.view_xx + (mww2 / gdata->data.gsc + 84);
    gdata->data.bpy2 = gdata->data.view_yy + (mhh2 / gdata->data.gsc + 84);
    gdata->data.fpx1 = gdata->data.view_xx - (mww2 / gdata->data.gsc + 24);
    gdata->data.fpy1 = gdata->data.view_yy - (mhh2 / gdata->data.gsc + 24);
    gdata->data.fpx2 = gdata->data.view_xx + (mww2 / gdata->data.gsc + 24);
    gdata->data.fpy2 = gdata->data.view_yy + (mhh2 / gdata->data.gsc + 24);
  }

  int foods_len = tdarray_length(gdata->data.foods);
  int preys_len = tdarray_length(gdata->data.preys);
  if (mode->uniform_food_color) {
    for (int i = foods_len - 1; i >= 0; i--) {
      food* fo = gdata->data.foods + i;

      if (usrs->hotkeys[HOTKEY_BIG_FOOD].active && fo->sz < 10) continue;

      if (fo->rx >= gdata->data.fpx1 && fo->ry >= gdata->data.fpy1 &&
          fo->rx <= gdata->data.fpx2 && fo->ry <= gdata->data.fpy2) {
        float d =
            gdata->fsz[fo->cv2] * gdata->data.gsc * fo->rad * mode->food_scale;
        vec3s c = {mode->food_color[0], mode->food_color[1],
                   mode->food_color[2]};

        float fx =
            mww2 + gdata->data.gsc * (fo->rx - gdata->data.view_xx) - d * 0.5;
        float fy =
            mhh2 + gdata->data.gsc * (fo->ry - gdata->data.view_yy) - d * 0.5;

        if (render_food_glow) {
          float gd = d * 1.85f;
          fd_renderer_push(usr->r->fdr,
                           &(fd_instance){{fx - (gd - d) * 0.5f,
                                           fy - (gd - d) * 0.5f, gd},
                                          (vec4s){c.r, c.g, c.b,
                                                  fo->fr * 0.20f},
                                          1.0f});
        }
        fd_renderer_push(usr->r->fdr,
                         &(fd_instance){{fx, fy, d},
                                        (vec4s){c.r, c.g, c.b, fo->fr},
                                        mode->food_flicker *
                                            (.5 + .5 * cosf(fo->gfr / 13))});
      }
    }
  } else {
    for (int i = foods_len - 1; i >= 0; i--) {
      food* fo = gdata->data.foods + i;

      if (usrs->hotkeys[HOTKEY_BIG_FOOD].active && fo->sz < 10) continue;

      if (fo->rx >= gdata->data.fpx1 && fo->ry >= gdata->data.fpy1 &&
          fo->rx <= gdata->data.fpx2 && fo->ry <= gdata->data.fpy2) {
        float d =
            gdata->fsz[fo->cv2] * gdata->data.gsc * fo->rad * mode->food_scale;
        vec3s c = gdata->cg_colors[fo->cv];

        float fx =
            mww2 + gdata->data.gsc * (fo->rx - gdata->data.view_xx) - d * 0.5;
        float fy =
            mhh2 + gdata->data.gsc * (fo->ry - gdata->data.view_yy) - d * 0.5;

        if (render_food_glow) {
          float gd = d * 1.85f;
          fd_renderer_push(usr->r->fdr,
                           &(fd_instance){{fx - (gd - d) * 0.5f,
                                           fy - (gd - d) * 0.5f, gd},
                                          (vec4s){c.r, c.g, c.b,
                                                  fo->fr * 0.20f},
                                          1.0f});
        }
        fd_renderer_push(usr->r->fdr,
                         &(fd_instance){{fx, fy, d},
                                        (vec4s){c.r, c.g, c.b, fo->fr},
                                        mode->food_flicker *
                                            (.5 + .5 * cosf(fo->gfr / 13))});
      }
    }
  }

  for (int i = preys_len - 1; i >= 0; i--) {
    prey* pr = gdata->data.preys + i;
    float tx = pr->xx + pr->fx;
    float ty = pr->yy + pr->fy;
    float px = mww2 + gdata->data.gsc * (tx - gdata->data.view_xx);
    float py = mhh2 + gdata->data.gsc * (ty - gdata->data.view_yy);
    if (px >= -50 && py >= -50 && px <= mwwp50 && py <= mhhp50) {
      if (pr->eaten) {
        snake* o = get_snake(gdata, pr->ebid);
        float k = powf(pr->eaten_fr, 2);
        tx += (o->xx + o->fx + cosf(o->ang + o->fa) * (43 - k * 24) * (1 - k) -
               tx) *
              k;
        ty += (o->yy + o->fy + sinf(o->ang + o->fa) * (43 - k * 24) * (1 - k) -
               ty) *
              k;
        px = mww2 + gdata->data.gsc * (tx - gdata->data.view_xx);
        py = mhh2 + gdata->data.gsc * (ty - gdata->data.view_yy);
      }

      float d = gdata->psz[pr->cv2] * gdata->data.gsc * pr->rad;
      vec3s c = gdata->cg_colors[pr->cv];
      float fx = px - d * 0.5f;
      float fy = py - d * 0.5f;

      fd_renderer_push_p(usr->r->fdr,
                         &(fd_instance){{fx, fy, d},
                                        (vec4s){c.r, c.g, c.b, pr->fr * 0.75f},
                                        .5 + .5 * cosf(pr->gfr / 13)});
    }
  }

  int snakes_len = tdarray_length(gdata->data.snakes);
  for (int i = snakes_len - 1; i >= 0; i--) {
    snake* o = gdata->data.snakes + i;
    int iiv = false;

    int pts_len = tdarray_length(o->pts);
    for (int j = pts_len - 1; j >= 0; j--) {
      body_part* po = o->pts + j;
      float px = po->xx + po->fx;
      float py = po->yy + po->fy;

      if (px >= gdata->data.bpx1 && py >= gdata->data.bpy1 &&
          px <= gdata->data.bpx2 && py <= gdata->data.bpy2) {
        iiv = true;
        break;
      }
    }
    if (o->iiv != iiv) {
      o->iiv = iiv;
      if (iiv) o->ehang = o->wehang = o->ang;
    }
  }

  igPushFont(
      usr->imgui_data.mono_font[usrs->snake_names_font_size],
      usr->imgui_data.mono_font[usrs->snake_names_font_size]->LegacySize);
  for (int i = snakes_len - 1; i >= 0; i--) {
    snake* o = gdata->data.snakes + i;
    int sct = o->sct + o->rsc;

    if (o->iiv) {
      float hx = o->xx + o->fx;
      float hy = o->yy + o->fy;
      float px = hx;
      float py = hy;
      float a = o->alive_amt * (1 - o->dead_amt);

      if (usrs->hotkeys[HOTKEY_SHOW_NAMES].active) {
        int score = (int)floorf((gdata->data.fpsls[sct] +
                                 o->fam / gdata->data.fmlts[sct] - 1) *
                                    15 -
                                5) /
                    1;
        score = GLM_MIN(GLM_MAX(score, 0), 999999);
        double score_rep = score / 1000.0;
        char nk_label_buff[MAX_NICKNAME_LEN + 1 + 7 + 1] = {0};
        char score_rep_str[9] = {0};
        sprintf(score_rep_str, usrs->mode_nicks_plus[mode_index]
                                   ? " @%.1fK" : " %.1fK",
                score_rep);
        sprintf(nk_label_buff, "%s%s", o->nk, score_rep_str);

        if (o->id != gdata->data.snake_id || usrs->show_own_nickname_ingame) {
          float ntx = o->xx + o->fx;
          float nty = o->yy + o->fy;

          /* Team names intentionally use a larger bold face than ordinary
             names so a teammate remains readable during fast movement. */
          bool vlither_teammate =
              vlither_chat_is_snake_player(o->id, usrs->server_address);
          bool ntl_teammate = !vlither_teammate &&
              ntl_team_is_snake_teammate(o->ntl_id, usrs->server_address);
          int team_status = ntl_team_leaderboard_status(
              o->nk, usrs->server_address);
          bool sos_teammate = team_status == 3;
          if (team_status == 2) vlither_teammate = true;
          if (team_status == 1) ntl_teammate = true;
          bool team_teammate = vlither_teammate || ntl_teammate || sos_teammate;
          if (team_teammate)
            igPushFont(usr->imgui_data.mono_font_bold[FONT_SIZE_LARGE],
                       usr->imgui_data.mono_font_bold[FONT_SIZE_LARGE]->LegacySize);

          ImVec2 tsize;
          ImVec2 nsize;
          igCalcTextSize(&nsize, o->nk, NULL, false, -1);
          igCalcTextSize(&tsize, nk_label_buff, NULL, false, -1);

          ntx = mww2 + (ntx - gdata->data.view_xx) * gdata->data.gsc;
          nty = mhh2 + (nty - gdata->data.view_yy) * gdata->data.gsc;

          vec3s* scolor =
              gdata->cg_colors +
              (o->cusk ? o->cusk_data[0] : gdata->default_skins[o->cv][1]);

          vec3 ncolor;
          glm_vec3_lerp((float*)scolor, (vec3){1, 1, 1}, mode->player_names_outline ? 0.7f : 0.6f, ncolor);
          vec3 lcolor;
          glm_vec3_lerp((float*)scolor, (vec3){1, 1, 1}, mode->player_names_outline ? 0.8f : 0.7f, lcolor);

          /* Team-network name highlighting. Vlither presence has priority so
             a Vlither Android/key teammate is always yellow; otherwise an
             NTL teammate is green. */
          if (sos_teammate) {
            ncolor[0] = 1.00f;
            ncolor[1] = 0.08f;
            ncolor[2] = 0.10f;
          } else if (vlither_teammate) {
            ncolor[0] = 1.00f;
            ncolor[1] = 0.82f;
            ncolor[2] = 0.10f;
          } else if (ntl_teammate) {
            ncolor[0] = 0.20f;
            ncolor[1] = 1.00f;
            ncolor[2] = 0.35f;
          }

          ntx = ntx - (usrs->snake_scores ? tsize.x : nsize.x) * 0.5f;
          if (usrs->mode_names_on_top[mode_index])
            nty = nty - tsize.y - 19 - 8 * o->sc * gdata->data.gsc;
          else
            nty = nty + 32 + 11 * o->sc * gdata->data.gsc;

          bool strong_name_outline = mode->player_names_outline || team_teammate;
          /* A radius of two creates 24 extra text draws per teammate name.
             Keep the stronger normal-quality outline, but use the regular
             eight-neighbour outline in Performance mode. */
          int name_outline_radius = team_teammate && !usrs->performance_mode
                                        ? 2 : 1;
          if (strong_name_outline) {
            for (int x = -name_outline_radius; x <= name_outline_radius; x++) {
              for (int y = -name_outline_radius; y <= name_outline_radius; y++) {
                if (x == 0 && y == 0) continue;
                ImDrawList_AddText_Vec2(
                    igGetWindowDrawList(), (ImVec2){ntx + x, nty + y},
                    igColorConvertFloat4ToU32((ImVec4){0, 0, 0, a}), o->nk,
                    NULL);
              }
            }

            if (usrs->snake_scores) {
              for (int x = -name_outline_radius; x <= name_outline_radius; x++) {
                for (int y = -name_outline_radius; y <= name_outline_radius; y++) {
                  ImDrawList_AddText_Vec2(
                      igGetWindowDrawList(),
                      (ImVec2){ntx + nsize.x + x, nty + y},
                      igColorConvertFloat4ToU32((ImVec4){0, 0, 0, a}),
                      score_rep_str, NULL);
                }
              }
            }
          }

          ImDrawList_AddText_Vec2(
              igGetWindowDrawList(), (ImVec2){ntx, nty},
              igColorConvertFloat4ToU32(
                  (ImVec4){ncolor[0], ncolor[1], ncolor[2],
                           team_teammate ? a : (mode->player_names_outline ? a : 0.5f * a)}),
              o->nk, NULL);

          if (usrs->snake_scores) {
            if (!team_teammate)
              igPushFont(
                  usr->imgui_data.mono_font_bold[usrs->snake_names_font_size],
                  usr->imgui_data.mono_font_bold[usrs->snake_names_font_size]
                      ->LegacySize);
            ImDrawList_AddText_Vec2(
                igGetWindowDrawList(), (ImVec2){ntx + nsize.x, nty},
                igColorConvertFloat4ToU32(
                    (ImVec4){lcolor[0], lcolor[1], lcolor[2],
                             strong_name_outline ? a : 0.7f * a}),
                score_rep_str, NULL);
            if (!team_teammate) igPopFont();
          }
          if (team_teammate) igPopFont();
        }
      }

      if (sct >= 2) {
        float fang = o->ehang;
        float ssc = o->sc;
        float lsz = 29 * ssc;
        float rl = o->cfl;
        int pts_len = tdarray_length(o->pts);
        int po_i = pts_len - 1;

        lsz *= .5;
        float ix1 = 0, iy1 = 0, ix2 = 0, iy2 = 0, ax1 = 0, ay1 = 0, ax2 = 0,
              ay2 = 0, cx2 = 0, cy2 = 0, pax1 = 0, pay1 = 0;
        int bp = 0;
        px = hx;
        py = hy;
        ax2 = px;
        ay2 = py;
        float ax = px;
        float ay = py;
        bp = 0;
        float px2 = 0, py2 = 0;
        float px3 = 0, py3 = 0;
        int po2_i = 0, po3_i = 0;
        int lpo_i = 0;
        float d = 0, d2 = 0;
        float dx = 0, dy = 0;
        float d3 = 0;
        float tx = 0, ty = 0;
        float ox = 0, oy = 0;
        float rx = 0, ry = 0;
        tx = 0;
        ty = 0;
        float j = 0, k = 0, l = 0, m = 0;
        float j2 = 0;
        float k2 = 0;
        float irl = 0;
        float wk = 0;
        float wwk = 0;
        float nkr = 0;
        float msl = o->msl;
        float mct = 6 / (mode->qsm * o->sep / 6.0f);

        float omct = mct;
        float rmct = 1 / mct;
        float sep = msl / mct;
        int ll = 0;
        po_i = pts_len - 1;
        px = o->pts[po_i].xx + o->pts[po_i].fx;
        py = o->pts[po_i].yy + o->pts[po_i].fy;
        d = sqrtf(powf(hx - px, 2) + powf(hy - py, 2));
        dx = (hx - px) / d;
        dy = (hy - py) / d;
        nkr = d / msl;
        int gpt_i, lgpt_i;
        int gpt2_i, lgpt2_i;
        int gpo_i;
        int q = 0;
        po3_i = pts_len - 2;
        po2_i = pts_len - 1;
        px = hx;
        py = hy;
        px2 = o->pts[po2_i].xx + o->pts[po2_i].fx;
        py2 = o->pts[po2_i].yy + o->pts[po2_i].fy;

        if (po3_i >= 0) {
          px3 = o->pts[po3_i].xx + o->pts[po3_i].fx;
          py3 = o->pts[po3_i].yy + o->pts[po3_i].fy;
        }
        if (d > msl) {
          px = px2 + dx * msl;
          py = py2 + dy * msl;
        }
        ax1 = px + (px2 - px) * .5;
        ay1 = py + (py2 - py) * .5;
        if (nkr < 1) {
          ax1 += (px - ax1) * (1 - nkr);
          ay1 += (py - ay1) * (1 - nkr);
        }
        ax2 = px3 + (px2 - px3) * .5;
        ay2 = py3 + (py2 - py3) * .5;
        d2 = sqrtf(powf(hx - ax1, 2) + powf(hy - ay1, 2));
        k = sep;
        m = 1;
        gpt_i = arp(o, q, hx, hy);
        q++;
        o->gptz[gpt_i].d = 0;
        lgpt_i = gpt_i;
        wk++;
        while (k < d2) {
          tx = hx - m * dx * sep;
          ty = hy - m * dy * sep;
          gpt_i = arp(o, q, tx, ty);
          q++;
          d = sep;
          o->gptz[gpt_i].d = d;
          lgpt_i = gpt_i;
          wk++;
          if (ll == 1) {
            ll = 2;
            break;
          }
          rl -= rmct;
          if (rl <= 0) {
            ll = 1;
            m += (rmct + rl) / rmct;
            k += sep * (rmct + rl) / rmct;
          } else {
            m++;
            k += sep;
          }
        }
        irl = (k - d2) / msl;
        if (ll <= 1) {
          if (rl >= -1E-4 && rl <= 0) rl = 0;
          if (rl >= 0 || ll == 1) {
            if (nkr < 1) {
              px2 += (ax2 - px2) * .5 * (1 - nkr);
              py2 += (ay2 - py2) * .5 * (1 - nkr);
            }
            m = .5 + nkr - d2 / msl;
            while (irl >= 0 && irl < m) {
              k = irl / m;
              ix1 = ax1 + (px2 - ax1) * k;
              iy1 = ay1 + (py2 - ay1) * k;
              ix2 = px2 + (ax2 - px2) * k;
              iy2 = py2 + (ay2 - py2) * k;
              rx = ix1 + (ix2 - ix1) * k;
              ry = iy1 + (iy2 - iy1) * k;
              gpt_i = arp(o, q, rx, ry);
              q++;
              d = sqrtf(powf(o->gptz[gpt_i].xx - o->gptz[lgpt_i].xx, 2) +
                        powf(o->gptz[gpt_i].yy - o->gptz[lgpt_i].yy, 2));
              o->gptz[gpt_i].d = d;
              lgpt_i = gpt_i;
              wk++;
              if (ll == 1) {
                ll = 2;
                break;
              }
              rl -= rmct;
              if (rl <= 0) {
                ll = 1;
                irl += rmct + rl;
                rl = 0;
              } else
                irl += rmct;
            }
            irl -= m;
          }
          if (rl >= -1E-4 && rl <= 0) rl = 0;
        }
        int lj = tdarray_length(o->pts);
        pts_len = tdarray_length(o->pts);
        bool wsirl = false;
        if (ll <= 1) {
          wsirl = false;
          if (rl >= 0 || ll == 1) {
            float rmr = 0;
            po_i = lj - 1;
            for (j = pts_len - 1; j >= 2; j--) {
              lj = j;
              lpo_i = po_i;
              po3_i = (int)(j - 2);
              po2_i = (int)(j - 1);
              po_i = (int)j;
              px = o->pts[po_i].xx + o->pts[po_i].fx;
              py = o->pts[po_i].yy + o->pts[po_i].fy;
              px2 = o->pts[po2_i].xx + o->pts[po2_i].fx;
              py2 = o->pts[po2_i].yy + o->pts[po2_i].fy;
              px3 = o->pts[po3_i].xx + o->pts[po3_i].fx;
              py3 = o->pts[po3_i].yy + o->pts[po3_i].fy;
              ax1 = px + (px2 - px) * .5;
              ay1 = py + (py2 - py) * .5;
              ax2 = px2 + (px3 - px2) * .5;
              ay2 = py2 + (py3 - py2) * .5;
              m = o->pts[po_i].ltn + o->pts[po_i].fltn;
              wwk = omct * 2 + 2;
              if (o->pts[po_i].smu != o->pts[lpo_i].smu ||
                  o->pts[po_i].fsmu != o->pts[lpo_i].fsmu) {
                irl *= (o->pts[lpo_i].smu + o->pts[lpo_i].fsmu) /
                       (o->pts[po_i].smu + o->pts[po_i].fsmu);
                mct = omct * (o->pts[po_i].smu + o->pts[po_i].fsmu);
                rmct = 1 / mct;
                sep = msl / mct;
              }
              rl -= rmr * rmct;
              while (irl < m) {
                k = irl / m;
                ix1 = ax1 + (px2 - ax1) * k;
                iy1 = ay1 + (py2 - ay1) * k;
                ix2 = px2 + (ax2 - px2) * k;
                iy2 = py2 + (ay2 - py2) * k;
                rx = ix1 + (ix2 - ix1) * k;
                ry = iy1 + (iy2 - iy1) * k;
                gpt_i = arp(o, q, rx, ry);
                q++;
                if (wk <= wwk) {
                  d = sqrtf(powf(o->gptz[gpt_i].xx - o->gptz[lgpt_i].xx, 2) +
                            powf(o->gptz[gpt_i].yy - o->gptz[lgpt_i].yy, 2));
                  o->gptz[gpt_i].d = d;
                  lgpt_i = gpt_i;
                  wk++;
                }
                if (ll == 1) {
                  ll = 2;
                  j = -9999;
                  break;
                }
                rl -= rmct;
                if (rl <= 0) {
                  ll = 1;
                  irl += rmct + rl;
                } else
                  irl += rmct;
              }
              irl -= m;
              rmr = irl / rmct;
              rl += irl;
              wsirl = true;
            }
          }
          if (wsirl) rl -= irl;
        }
        if (ll <= 1) {
          if (rl >= -1E-4 && rl <= 0) rl = 0;
          if (rl >= 0 || ll == 1) {
            po_i = (int)(lj - 1);
            po2_i = (int)(lj - 2);
            if (po_i >= 0) {
              px = o->pts[po_i].xx + o->pts[po_i].fx;
              py = o->pts[po_i].yy + o->pts[po_i].fy;
            }
            px2 = o->pts[po2_i].xx + o->pts[po2_i].fx;
            py2 = o->pts[po2_i].yy + o->pts[po2_i].fy;
            while (rl >= 0 || ll == 1) {
              rx = px2 - (px - px2) * (irl - .5);
              ry = py2 - (py - py2) * (irl - .5);
              gpt_i = arp(o, q, rx, ry);
              q++;
              if (wk <= wwk) {
                d = sqrtf(powf(o->gptz[gpt_i].xx - o->gptz[lgpt_i].xx, 2) +
                          powf(o->gptz[gpt_i].yy - o->gptz[lgpt_i].yy, 2));
                o->gptz[gpt_i].d = d;
                lgpt_i = gpt_i;
                wk++;
              }
              if (ll == 1) {
                ll = 2;
                j = -9999;
                break;
              }
              rl -= rmct;
              if (rl <= 0) {
                ll = 1;
                irl += rmct + rl;
              } else
                irl += rmct;
              if (rl >= -1E-4 && rl <= 0) rl = 0;
            }
          }
        }
        k = wk - 1;
        int gptz_len = tdarray_length(o->gptz);
        if (k >= gptz_len) k = gptz_len;

        if (k >= 3) {
          d3 = 0;
          for (j = 0; j < k - 1; j++) {
            gpt_i = j;
            d3 += o->gptz[gpt_i].d;
          }
          lgpt_i = 0;
          lgpt2_i = 0;
          m = d3 / (k - 2);
          j = 1;
          j2 = 1;
          float v = m;
          for (j = 0; j < k; j++) {
            o->gptz[(int)j].ox = o->gptz[(int)j].xx;
            o->gptz[(int)j].oy = o->gptz[(int)j].yy;
          }
          for (j = 1; j < k; j++) {
            gpt_i = j;
            while (true) {
              gpt2_i = j2;
              if (v < o->gptz[gpt2_i].d) {
                o->gptz[gpt_i].xx = o->gptz[lgpt2_i].ox +
                                    (o->gptz[gpt2_i].ox - o->gptz[lgpt2_i].ox) *
                                        v / o->gptz[gpt2_i].d;
                o->gptz[gpt_i].yy = o->gptz[lgpt2_i].oy +
                                    (o->gptz[gpt2_i].oy - o->gptz[lgpt2_i].oy) *
                                        v / o->gptz[gpt2_i].d;
                o->gptz[gpt_i].xx +=
                    (o->gptz[gpt_i].ox - o->gptz[gpt_i].xx) * powf(j / k, 2);
                o->gptz[gpt_i].yy +=
                    (o->gptz[gpt_i].oy - o->gptz[gpt_i].yy) * powf(j / k, 2);
                v += m;
                break;
              } else {
                v -= o->gptz[gpt2_i].d;
                lgpt2_i = gpt2_i;
                j2++;
                if (j2 >= k) {
                  j = k + 1;
                  break;
                }
              }
            }
            lgpt_i = gpt_i;
          }
        }

        float lpx = 0, lpy = 0;
        for (j = 0; j < q; j++) {
          px = o->gptz[(int)j].xx;
          py = o->gptz[(int)j].yy;
          gdata->data.pbx[bp] = px;
          gdata->data.pby[bp] = py;
          gdata->data.pba[bp] = 0;

          if (px >= gdata->data.bpx1 && py >= gdata->data.bpy1 &&
              px <= gdata->data.bpx2 && py <= gdata->data.bpy2)
            gdata->data.pbu[bp] = 2;

          if (bp >= 1) {
            tx = px - lpx;
            ty = py - lpy;
            gdata->data.pba[bp] = atan2f(ty, tx);
          }
          lpx = px;
          lpy = py;
          bp++;
        }

        if (q >= 2) {
          gdata->data.pba[0] = gdata->data.pba[1];
          o->wehang = gdata->data.pba[1] + PI;
        } else
          o->wehang = o->ang;

        float shsz = gdata->data.gsc * lsz * 1.5f;
        a *= a;

        float om = 0;
        float mr = 0;
        // NTL-style transparent snake body. This applies to textured, solid,
        // and flat render modes instead of only the skinless modes.
        float skin_alpha = mode->transparent_skin
                               ? usrs->transparent_skin_opacity[mode_index]
                               : 1.0f;
        if (o->id == gdata->data.snake_id && usrs->own_skin_invisible)
          skin_alpha = 0.0f;

        bool is_local_snake = o->id == gdata->data.snake_id;
        bool is_team_snake = !is_local_snake &&
                             redraw_snake_is_teammate(o, usrs);
        int snake_render_mode = mode->render_mode;
        if (usrs->mode_high_visibility_skins[mode_index] ||
            usrs->mode_skinless_peek[mode_index]) {
          if (usrs->mode_skinless_peek[mode_index] &&
              g_ntl_skin_peek_active)
            snake_render_mode = 0;
          else if ((is_local_snake && usrs->mode_own_true_skin[mode_index]) ||
                   (is_team_snake && usrs->mode_team_true_skin[mode_index]))
            snake_render_mode = 0;
          else
            snake_render_mode = 1;
        }

        if (snake_render_mode == 0) {
          float shadow_strength =
              0.25f * usrs->snake_shadow_strength[mode_index] * skin_alpha;

          if (render_shadows) {

            int start = bp >= 4 ? bp - 4 : 0;
            for (j = start; j < bp; j++) {
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                bp_renderer_push(
                    usr->r->bpr,
                    &(bp_instance){{fix - shsz, fiy - shsz, 2 * shsz,
                                    gdata->data.pba[(int)j]},
                                   gdata->SHADOW_UV,
                                   {0, 0, 0, shadow_strength * a}});
              }
            }
          }

          if (o->cusk) {
            for (j = bp - 1; j >= 0; j--)
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                if (j >= 4 && render_shadows) {
                  k = j - 4;
                  if (gdata->data.pbu[(int)k] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)k];
                    ty = gdata->data.pby[(int)k];
                    float fix =
                        ((tx - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                    float fiy =
                        ((ty - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                    bp_renderer_push(
                        usr->r->bpr,
                        &(bp_instance){{fix - shsz, fiy - shsz, 2 * shsz,
                                        gdata->data.pba[(int)j]},
                                       gdata->SHADOW_UV,
                                       {0, 0, 0, shadow_strength * a}});
                  }
                }

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                int cg_id = o->cusk_data[(int)j % o->cusk_len];

                bp_renderer_push(usr->r->bpr,
                                 &(bp_instance){{fix - (gdata->data.gsc * lsz),
                                                 fiy - (gdata->data.gsc * lsz),
                                                 gdata->data.gsc * 2 * lsz,
                                                 gdata->data.pba[(int)j]},
                                                gdata->cg_uvs[cg_id],
                                                {1, 1, 1, a * skin_alpha}});
              }
          } else {
            for (j = bp - 1; j >= 0; j--)
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                if (j >= 4 && render_shadows) {
                  k = j - 4;
                  if (gdata->data.pbu[(int)k] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)k];
                    ty = gdata->data.pby[(int)k];
                    float fix =
                        ((tx - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                    float fiy =
                        ((ty - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                    bp_renderer_push(
                        usr->r->bpr,
                        &(bp_instance){{fix - shsz, fiy - shsz, 2 * shsz,
                                        gdata->data.pba[(int)j]},
                                       gdata->SHADOW_UV,
                                       {0, 0, 0, shadow_strength * a}});
                  }
                }

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                int default_skin_len = gdata->default_skins[o->cv][0];
                int cg_id =
                    gdata
                        ->default_skins[o->cv][1 + ((int)j % default_skin_len)];
                float se = gdata->worm_effect[(int)j % WORM_EFFECT_LEN];

                bp_renderer_push(usr->r->bpr,
                                 &(bp_instance){{fix - (gdata->data.gsc * lsz),
                                                 fiy - (gdata->data.gsc * lsz),
                                                 gdata->data.gsc * 2 * lsz,
                                                 gdata->data.pba[(int)j]},
                                                gdata->cg_uvs[cg_id],
                                                {se, se, se, a * skin_alpha}});
              }
          }
        } else if (snake_render_mode == 1) {
          float skinless_a = skin_alpha;
          if (render_shadows) {

            int start = bp >= 4 ? bp - 4 : 0;
            for (j = start; j < bp; j++) {
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                bp_renderer_push(
                    usr->r->bpr,
                    &(bp_instance){{fix - (lsz * gdata->data.gsc + 1),
                                    fiy - (lsz * gdata->data.gsc + 1),
                                    (lsz * gdata->data.gsc + 1) * 2,
                                    gdata->data.pba[(int)j]},
                                   gdata->cg_uvs[BLANK_UV],
                                   {0, 0, 0, a * a * skin_alpha *
                                                  usrs->snake_shadow_strength[mode_index]}});
              }
            }
          }

          if (o->cusk) {
            for (j = bp - 1; j >= 0; j--)
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                if (j >= 4 && render_shadows) {
                  k = j - 4;
                  if (gdata->data.pbu[(int)k] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)k];
                    ty = gdata->data.pby[(int)k];
                    float fix =
                        ((tx - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                    float fiy =
                        ((ty - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                    bp_renderer_push(
                        usr->r->bpr,
                        &(bp_instance){{fix - (lsz * gdata->data.gsc + 1),
                                        fiy - (lsz * gdata->data.gsc + 1),
                                        (lsz * gdata->data.gsc + 1) * 2,
                                        gdata->data.pba[(int)j]},
                                       gdata->cg_uvs[BLANK_UV],
                                       {0, 0, 0, a * a * skin_alpha *
                                                      usrs->snake_shadow_strength[mode_index]}});
                  }
                }

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                int cg_id = o->cusk_data[(int)j % o->cusk_len];
                vec3s* cg_col = gdata->cg_colors + cg_id;

                bp_renderer_push(
                    usr->r->bpr,
                    &(bp_instance){
                        {fix - (gdata->data.gsc * lsz),
                         fiy - (gdata->data.gsc * lsz),
                         gdata->data.gsc * 2 * lsz, gdata->data.pba[(int)j]},
                        gdata->cg_uvs[BLANK_UV],
                        {cg_col->r, cg_col->g, cg_col->b, a * skinless_a}});
              }
          } else {
            for (j = bp - 1; j >= 0; j--)
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                if (j >= 4 && render_shadows) {
                  k = j - 4;
                  if (gdata->data.pbu[(int)k] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)k];
                    ty = gdata->data.pby[(int)k];
                    float fix =
                        ((tx - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                    float fiy =
                        ((ty - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                    bp_renderer_push(
                        usr->r->bpr,
                        &(bp_instance){{fix - (lsz * gdata->data.gsc + 1),
                                        fiy - (lsz * gdata->data.gsc + 1),
                                        (lsz * gdata->data.gsc + 1) * 2,
                                        gdata->data.pba[(int)j]},
                                       gdata->cg_uvs[BLANK_UV],
                                       {0, 0, 0, a * a * skin_alpha *
                                                      usrs->snake_shadow_strength[mode_index]}});
                  }
                }

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                int default_skin_len = gdata->default_skins[o->cv][0];
                int cg_id =
                    gdata
                        ->default_skins[o->cv][1 + ((int)j % default_skin_len)];
                vec3s* cg_col = gdata->cg_colors + cg_id;

                bp_renderer_push(
                    usr->r->bpr,
                    &(bp_instance){
                        {fix - (gdata->data.gsc * lsz),
                         fiy - (gdata->data.gsc * lsz),
                         gdata->data.gsc * 2 * lsz, gdata->data.pba[(int)j]},
                        gdata->cg_uvs[BLANK_UV],
                        {cg_col->r, cg_col->g, cg_col->b, a * skinless_a}});
              }
          }
        } else if (snake_render_mode == 2) {
          float skinless_a = skin_alpha;
          if (render_shadows) {

            int start = bp >= 4 ? bp - 4 : 0;
            for (j = start; j < bp; j++) {
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                bp_renderer_push(
                    usr->r->bpr,
                    &(bp_instance){{fix - (lsz * gdata->data.gsc + 1),
                                    fiy - (lsz * gdata->data.gsc + 1),
                                    (lsz * gdata->data.gsc + 1) * 2,
                                    gdata->data.pba[(int)j]},
                                   gdata->cg_uvs[BLANK_UV],
                                   {0, 0, 0, a * a * skin_alpha *
                                                  usrs->snake_shadow_strength[mode_index]}});
              }
            }
          }

          if (o->cusk) {
            for (j = bp - 1; j >= 0; j--)
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                if (j >= 4 && render_shadows) {
                  k = j - 4;
                  if (gdata->data.pbu[(int)k] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)k];
                    ty = gdata->data.pby[(int)k];
                    float fix =
                        ((tx - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                    float fiy =
                        ((ty - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                    bp_renderer_push(
                        usr->r->bpr,
                        &(bp_instance){{fix - (lsz * gdata->data.gsc + 1),
                                        fiy - (lsz * gdata->data.gsc + 1),
                                        (lsz * gdata->data.gsc + 1) * 2,
                                        gdata->data.pba[(int)j]},
                                       gdata->cg_uvs[BLANK_UV],
                                       {0, 0, 0, a * a * skin_alpha *
                                                      usrs->snake_shadow_strength[mode_index]}});
                  }
                }

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                int cg_id = o->cusk_data[0];
                vec3s* cg_col = gdata->cg_colors + cg_id;

                bp_renderer_push(
                    usr->r->bpr,
                    &(bp_instance){
                        {fix - (gdata->data.gsc * lsz),
                         fiy - (gdata->data.gsc * lsz),
                         gdata->data.gsc * 2 * lsz, gdata->data.pba[(int)j]},
                        gdata->cg_uvs[BLANK_UV],
                        {cg_col->r, cg_col->g, cg_col->b, a * a * skinless_a}});
              }
          } else {
            for (j = bp - 1; j >= 0; j--)
              if (gdata->data.pbu[(int)j] >= 1) {
                px = gdata->data.pbx[(int)j];
                py = gdata->data.pby[(int)j];

                if (j >= 4 && render_shadows) {
                  k = j - 4;
                  if (gdata->data.pbu[(int)k] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)k];
                    ty = gdata->data.pby[(int)k];
                    float fix =
                        ((tx - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                    float fiy =
                        ((ty - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                    bp_renderer_push(
                        usr->r->bpr,
                        &(bp_instance){{fix - (lsz * gdata->data.gsc + 1),
                                        fiy - (lsz * gdata->data.gsc + 1),
                                        (lsz * gdata->data.gsc + 1) * 2,
                                        gdata->data.pba[(int)j]},
                                       gdata->cg_uvs[BLANK_UV],
                                       {0, 0, 0, a}});
                  }
                }

                float fix =
                    ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
                float fiy =
                    ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

                int default_skin_len = gdata->default_skins[o->cv][0];
                int cg_id = gdata->default_skins[o->cv][1];
                vec3s* cg_col = gdata->cg_colors + cg_id;

                bp_renderer_push(
                    usr->r->bpr,
                    &(bp_instance){
                        {fix - (gdata->data.gsc * lsz),
                         fiy - (gdata->data.gsc * lsz),
                         gdata->data.gsc * 2 * lsz, gdata->data.pba[(int)j]},
                        gdata->cg_uvs[BLANK_UV],
                        {cg_col->r, cg_col->g, cg_col->b, a * skinless_a}});
              }
          }
        }

        bool is_local_line = o->id == gdata->data.snake_id;
        bool draw_center_line =
            (is_local_line && mode->center_line) ||
            (!is_local_line && usrs->center_line_others[mode_index]);
        if (draw_center_line && skin_alpha > 0.001f && bp >= 2) {
          int line_cg = o->cusk && o->cusk_len > 0
                            ? o->cusk_data[0]
                            : gdata->default_skins[o->cv][1];
          vec3s line_base = gdata->cg_colors[line_cg];
          float luminance = line_base.r * 0.2126f + line_base.g * 0.7152f +
                            line_base.b * 0.0722f;
          float line_rgb = luminance > 0.82f ? 0.0f : 1.0f;
          const float line_thickness = 2.0f;
          for (j = 1; j < bp; j++) {
            float x1 = ((gdata->data.pbx[(int)j - 1] - gdata->data.view_xx) *
                        gdata->data.gsc) +
                       mww2;
            float y1 = ((gdata->data.pby[(int)j - 1] - gdata->data.view_yy) *
                        gdata->data.gsc) +
                       mhh2;
            float x2 = ((gdata->data.pbx[(int)j] - gdata->data.view_xx) *
                        gdata->data.gsc) +
                       mww2;
            float y2 = ((gdata->data.pby[(int)j] - gdata->data.view_yy) *
                        gdata->data.gsc) +
                       mhh2;

            const float line_margin = 8.0f;
            bool p1_visible = x1 >= -line_margin && y1 >= -line_margin &&
                              x1 <= ctx->size[0] + line_margin &&
                              y1 <= ctx->size[1] + line_margin;
            bool p2_visible = x2 >= -line_margin && y2 >= -line_margin &&
                              x2 <= ctx->size[0] + line_margin &&
                              y2 <= ctx->size[1] + line_margin;
            if (!p1_visible && !p2_visible) continue;

            float dx_line = x2 - x1;
            float dy_line = y2 - y1;
            float segment_length = sqrtf(dx_line * dx_line + dy_line * dy_line);
            if (segment_length <= 0.01f) continue;

            float capsule_length = segment_length + line_thickness;
            float center_x = (x1 + x2) * 0.5f;
            float center_y = (y1 + y2) * 0.5f;
            float line_angle = atan2f(dy_line, dx_line);

            bp_renderer_push(
                usr->r->bpr,
                &(bp_instance){
                    {center_x - capsule_length * 0.5f,
                     center_y - line_thickness * 0.5f, capsule_length,
                     line_angle},
                    gdata->cg_uvs[BLANK_UV],
                    {line_rgb, line_rgb, line_rgb, 0.85f * a},
                    {line_thickness, 1}});
          }
        }

        if (mode->death_effect && o->dead) {
          float falf = (.15 + .15 * fabsf(sinf(5 * PI * o->dead_amt))) *
                       sinf(PI * o->dead_amt);
          float dsz = gdata->data.gsc * lsz;

          for (j = bp - 1; j >= 0; j--)
            if (gdata->data.pbu[(int)j] == 2) {
              ox = tx;
              oy = ty;
              tx = gdata->data.pbx[(int)j];
              ty = gdata->data.pby[(int)j];
              if (tx > ox)
                d2 = tx - ox;
              else
                d2 = ox - tx;
              if (ty > oy)
                d2 += ty - oy;
              else
                d2 += oy - ty;
              d2 /= 6;
              if (d2 > 1) d2 = 1;
              px = gdata->data.pbx[(int)j];
              py = gdata->data.pby[(int)j];
              float alpha =
                  d2 * falf * (.6 + .4 * cosf(j / 4 - 15 * o->dead_amt));

              float fix = ((px - gdata->data.view_xx) * gdata->data.gsc) + mww2;
              float fiy = ((py - gdata->data.view_yy) * gdata->data.gsc) + mhh2;

              bp_renderer_push(
                  usr->r->bpr,
                  &(bp_instance){{fix - dsz, fiy - dsz, dsz * 2, 0},
                                 gdata->cg_uvs[BLANK_UV],
                                 {1, 1, 1, alpha * 0.5f}});
            }
        }

        if (mode->show_boost) {
          if (snake_render_mode == 2) {
            if (o->tsp > o->fsp) {
              m = a * fmaxf(0, fminf(1, (o->tsp - o->ssp) / (o->msp - o->ssp)));
              om = m * .37;
              mr = powf(m, .5);
              float glsz = (1 + mode->boost_type + mr) * gdata->data.gsc * lsz;
              float strength = 1 + mode->boost_type * mode->boost_strength;

              if (o->cusk) {
                for (j = bp - 1; j >= 0; j--)
                  if (gdata->data.pbu[(int)j] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)j];
                    ty = gdata->data.pby[(int)j];
                    if (tx > ox)
                      d2 = tx - ox;
                    else
                      d2 = ox - tx;
                    if (ty > oy)
                      d2 += ty - oy;
                    else
                      d2 += oy - ty;
                    d2 /= 6;
                    if (d2 > 1) d2 = 1;

                    float fix =
                        (tx - gdata->data.view_xx) * gdata->data.gsc + mww2;
                    float fiy =
                        (ty - gdata->data.view_yy) * gdata->data.gsc + mhh2;

                    float alpha = strength * d2 * a * mr * .38 *
                                  (.6 + .4 * cosf(j / 4 - 1.15 * o->sfr));
                    int cg_id = o->cusk_data[0];
                    vec3s* shc = gdata->cg_glow_colors + cg_id;

                    bst_renderer_push(
                        usr->r->bstb,
                        &(bst_instance){{fix - glsz, fiy - glsz, 2 * glsz},
                                        {shc->r, shc->g, shc->b, alpha}});
                  }
              } else {
                for (j = bp - 1; j >= 0; j--)
                  if (gdata->data.pbu[(int)j] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)j];
                    ty = gdata->data.pby[(int)j];
                    if (tx > ox)
                      d2 = tx - ox;
                    else
                      d2 = ox - tx;
                    if (ty > oy)
                      d2 += ty - oy;
                    else
                      d2 += oy - ty;
                    d2 /= 6;
                    if (d2 > 1) d2 = 1;

                    float fix =
                        (tx - gdata->data.view_xx) * gdata->data.gsc + mww2;
                    float fiy =
                        (ty - gdata->data.view_yy) * gdata->data.gsc + mhh2;

                    float alpha = strength * d2 * a * mr * .38 *
                                  (.6 + .4 * cosf(j / 4 - 1.15 * o->sfr));

                    int default_skin_len = gdata->default_skins[o->cv][0];
                    int cg_id = gdata->default_skins[o->cv][1];
                    vec3s* shc = gdata->cg_glow_colors + cg_id;

                    bst_renderer_push(
                        usr->r->bstb,
                        &(bst_instance){{fix - glsz, fiy - glsz, 2 * glsz},
                                        {shc->r, shc->g, shc->b, alpha}});
                  }
              }
            }

            if (mode->boost_type == 0 && o->tsp > o->fsp) {
              float glsz = lsz * 3 * gdata->data.gsc;
              float strength = 0.7f * mode->boost_strength;

              if (o->cusk) {
                for (j = bp - 1; j >= 0; j--)
                  if (gdata->data.pbu[(int)j] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)j];
                    ty = gdata->data.pby[(int)j];
                    if (tx > ox)
                      d2 = tx - ox;
                    else
                      d2 = ox - tx;
                    if (ty > oy)
                      d2 += ty - oy;
                    else
                      d2 += oy - ty;
                    d2 /= 6;
                    if (d2 > 1) d2 = 1;

                    float fix =
                        (tx - gdata->data.view_xx) * gdata->data.gsc + mww2;
                    float fiy =
                        (ty - gdata->data.view_yy) * gdata->data.gsc + mhh2;

                    float alpha = strength * d2 * a * om *
                                  (.5 + .5 * cosf(j / 4 - o->sfr));
                    int cg_id = o->cusk_data[0];
                    vec3s* shc = gdata->cg_glow_colors + cg_id;

                    bst_renderer_push(
                        usr->r->bsta,
                        &(bst_instance){{fix - glsz, fiy - glsz, 2 * glsz},
                                        {shc->r, shc->g, shc->b, alpha}});
                  }
              } else {
                for (j = bp - 1; j >= 0; j--)
                  if (gdata->data.pbu[(int)j] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)j];
                    ty = gdata->data.pby[(int)j];
                    if (tx > ox)
                      d2 = tx - ox;
                    else
                      d2 = ox - tx;
                    if (ty > oy)
                      d2 += ty - oy;
                    else
                      d2 += oy - ty;
                    d2 /= 6;
                    if (d2 > 1) d2 = 1;

                    float fix =
                        (tx - gdata->data.view_xx) * gdata->data.gsc + mww2;
                    float fiy =
                        (ty - gdata->data.view_yy) * gdata->data.gsc + mhh2;

                    float alpha = strength * d2 * a * om *
                                  (.5 + .5 * cosf(j / 4 - o->sfr));

                    int default_skin_len = gdata->default_skins[o->cv][0];
                    int cg_id = gdata->default_skins[o->cv][1];
                    vec3s* shc = gdata->cg_glow_colors + cg_id;

                    bst_renderer_push(
                        usr->r->bsta,
                        &(bst_instance){{fix - glsz, fiy - glsz, 2 * glsz},
                                        {shc->r, shc->g, shc->b, alpha}});
                  }
              }
            }
          } else {

            if (o->tsp > o->fsp) {
              m = a * fmaxf(0, fminf(1, (o->tsp - o->ssp) / (o->msp - o->ssp)));
              om = m * .37;
              mr = powf(m, .5);
              float glsz = (1 + mode->boost_type + mr) * gdata->data.gsc * lsz;
              float strength = 1 + mode->boost_type * mode->boost_strength;

              if (o->cusk) {
                for (j = bp - 1; j >= 0; j--)
                  if (gdata->data.pbu[(int)j] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)j];
                    ty = gdata->data.pby[(int)j];
                    if (tx > ox)
                      d2 = tx - ox;
                    else
                      d2 = ox - tx;
                    if (ty > oy)
                      d2 += ty - oy;
                    else
                      d2 += oy - ty;
                    d2 /= 6;
                    if (d2 > 1) d2 = 1;

                    float fix =
                        (tx - gdata->data.view_xx) * gdata->data.gsc + mww2;
                    float fiy =
                        (ty - gdata->data.view_yy) * gdata->data.gsc + mhh2;

                    float alpha = strength * d2 * a * mr * .38 *
                                  (.6 + .4 * cosf(j / 4 - 1.15 * o->sfr));
                    int cg_id = o->cusk_data[(int)j % o->cusk_len];
                    vec3s* shc = gdata->cg_glow_colors + cg_id;

                    bst_renderer_push(
                        usr->r->bstb,
                        &(bst_instance){{fix - glsz, fiy - glsz, 2 * glsz},
                                        {shc->r, shc->g, shc->b, alpha}});
                  }
              } else {
                for (j = bp - 1; j >= 0; j--)
                  if (gdata->data.pbu[(int)j] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)j];
                    ty = gdata->data.pby[(int)j];
                    if (tx > ox)
                      d2 = tx - ox;
                    else
                      d2 = ox - tx;
                    if (ty > oy)
                      d2 += ty - oy;
                    else
                      d2 += oy - ty;
                    d2 /= 6;
                    if (d2 > 1) d2 = 1;

                    float fix =
                        (tx - gdata->data.view_xx) * gdata->data.gsc + mww2;
                    float fiy =
                        (ty - gdata->data.view_yy) * gdata->data.gsc + mhh2;

                    float alpha = strength * d2 * a * mr * .38 *
                                  (.6 + .4 * cosf(j / 4 - 1.15 * o->sfr));

                    int default_skin_len = gdata->default_skins[o->cv][0];
                    int cg_id =
                        gdata->default_skins[o->cv]
                                            [1 + ((int)j % default_skin_len)];
                    vec3s* shc = gdata->cg_glow_colors + cg_id;

                    bst_renderer_push(
                        usr->r->bstb,
                        &(bst_instance){{fix - glsz, fiy - glsz, 2 * glsz},
                                        {shc->r, shc->g, shc->b, alpha}});
                  }
              }
            }

            if (mode->boost_type == 0 && o->tsp > o->fsp) {
              float glsz = lsz * 3 * gdata->data.gsc;
              float strength = 0.7f * mode->boost_strength;

              if (o->cusk) {
                for (j = bp - 1; j >= 0; j--)
                  if (gdata->data.pbu[(int)j] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)j];
                    ty = gdata->data.pby[(int)j];
                    if (tx > ox)
                      d2 = tx - ox;
                    else
                      d2 = ox - tx;
                    if (ty > oy)
                      d2 += ty - oy;
                    else
                      d2 += oy - ty;
                    d2 /= 6;
                    if (d2 > 1) d2 = 1;

                    float fix =
                        (tx - gdata->data.view_xx) * gdata->data.gsc + mww2;
                    float fiy =
                        (ty - gdata->data.view_yy) * gdata->data.gsc + mhh2;

                    float alpha = strength * d2 * a * om *
                                  (.5 + .5 * cosf(j / 4 - o->sfr));
                    int cg_id = o->cusk_data[(int)j % o->cusk_len];
                    vec3s* shc = gdata->cg_glow_colors + cg_id;

                    bst_renderer_push(
                        usr->r->bsta,
                        &(bst_instance){{fix - glsz, fiy - glsz, 2 * glsz},
                                        {shc->r, shc->g, shc->b, alpha}});
                  }
              } else {
                for (j = bp - 1; j >= 0; j--)
                  if (gdata->data.pbu[(int)j] == 2) {
                    ox = tx;
                    oy = ty;
                    tx = gdata->data.pbx[(int)j];
                    ty = gdata->data.pby[(int)j];
                    if (tx > ox)
                      d2 = tx - ox;
                    else
                      d2 = ox - tx;
                    if (ty > oy)
                      d2 += ty - oy;
                    else
                      d2 += oy - ty;
                    d2 /= 6;
                    if (d2 > 1) d2 = 1;

                    float fix =
                        (tx - gdata->data.view_xx) * gdata->data.gsc + mww2;
                    float fiy =
                        (ty - gdata->data.view_yy) * gdata->data.gsc + mhh2;

                    float alpha = strength * d2 * a * om *
                                  (.5 + .5 * cosf(j / 4 - o->sfr));

                    int default_skin_len = gdata->default_skins[o->cv][0];
                    int cg_id =
                        gdata->default_skins[o->cv]
                                            [1 + ((int)j % default_skin_len)];
                    vec3s* shc = gdata->cg_glow_colors + cg_id;

                    bst_renderer_push(
                        usr->r->bsta,
                        &(bst_instance){{fix - glsz, fiy - glsz, 2 * glsz},
                                        {shc->r, shc->g, shc->b, alpha}});
                  }
              }
            }
          }
        }

        float ed = 6 * ssc;
        float esp = 6 * ssc;
        float er = 6;
        default_skin_data* dfs = gdata->dfs + ((1 - o->cusk) * (1 + o->cv));
        float pr = dfs->pr;
        float iris_r = er * ssc * gdata->data.gsc;
        float pupil_r = pr * ssc * gdata->data.gsc;

        float ex = cosf(fang) * ed + cosf(fang - PI / 2) * (esp + .5);
        float ey = sinf(fang) * ed + sinf(fang - PI / 2) * (esp + .5);

        float ea = mode->death_effect
                       ? o->alive_amt * o->alive_amt * sqrtf(1 - o->dead_amt)
                       : a;

        bp_renderer_push(
            usr->r->bpr,
            &(bp_instance){
                {(mww2 + (ex + hx - gdata->data.view_xx) * gdata->data.gsc) -
                     iris_r,
                 (mhh2 + (ey + hy - gdata->data.view_yy) * gdata->data.gsc) -
                     iris_r,
                 iris_r * 2, 0},
                gdata->cg_uvs[BLANK_UV],
                {dfs->ec.r, dfs->ec.g, dfs->ec.b, ea}});

        ex = cosf(fang) * ed + cosf(fang + PI / 2) * (esp + .5);
        ey = sinf(fang) * ed + sinf(fang + PI / 2) * (esp + .5);

        bp_renderer_push(
            usr->r->bpr,
            &(bp_instance){
                {(mww2 + (ex + hx - gdata->data.view_xx) * gdata->data.gsc) -
                     iris_r,
                 (mhh2 + (ey + hy - gdata->data.view_yy) * gdata->data.gsc) -
                     iris_r,
                 iris_r * 2, 0},
                gdata->cg_uvs[BLANK_UV],
                {dfs->ec.r, dfs->ec.g, dfs->ec.b, ea}});

        ex = cosf(fang) * (ed + .5) + o->rex * ssc + cosf(fang - PI / 2) * esp;
        ey = sinf(fang) * (ed + .5) + o->rey * ssc + sinf(fang - PI / 2) * esp;

        bp_renderer_push(
            usr->r->bpr,
            &(bp_instance){
                {(mww2 + (ex + hx - gdata->data.view_xx) * gdata->data.gsc) -
                     pupil_r,
                 (mhh2 + (ey + hy - gdata->data.view_yy) * gdata->data.gsc) -
                     pupil_r,
                 pupil_r * 2, 0},
                gdata->cg_uvs[BLANK_UV],
                {dfs->ppc.r, dfs->ppc.g, dfs->ppc.b, ea}});

        ex = cosf(fang) * (ed + .5) + o->rex * ssc + cosf(fang + PI / 2) * esp;
        ey = sinf(fang) * (ed + .5) + o->rey * ssc + sinf(fang + PI / 2) * esp;

        bp_renderer_push(
            usr->r->bpr,
            &(bp_instance){
                {(mww2 + (ex + hx - gdata->data.view_xx) * gdata->data.gsc) -
                     pupil_r,
                 (mhh2 + (ey + hy - gdata->data.view_yy) * gdata->data.gsc) -
                     pupil_r,
                 pupil_r * 2, 0},
                gdata->cg_uvs[BLANK_UV],
                {dfs->ppc.r, dfs->ppc.g, dfs->ppc.b, ea}});

        bool hide_own_cosmetic = usrs->ntl_stealth_mode &&
                                 o->id == gdata->data.snake_id;
        bool hide_enemy_cosmetic =
                                    usrs->mode_hide_enemy_cosmetics[mode_index] &&
                                    o->id != gdata->data.snake_id &&
                                    !redraw_snake_is_teammate(o, usrs);
        if (mode->show_accessories && !hide_own_cosmetic &&
            !hide_enemy_cosmetic && o->accessory < NUM_ACCESSORIES) {
          accessory_data* acc = gdata->accessories + o->accessory;
          ex = acc->of * cosf(fang) * ed;
          ey = acc->of * sinf(fang) * ed;
          m = lsz * gdata->data.gsc * acc->sc;
          float acx = mww2 + (ex + hx - gdata->data.view_xx) * gdata->data.gsc;
          float acy = mhh2 + (ey + hy - gdata->data.view_yy) * gdata->data.gsc;

          bp_renderer_push(
              usr->r->bpr,
              &(bp_instance){
                  {acx - m, acy - m, m * 2, fang}, acc->uv, {1, 1, 1, ea}});
        }

      }
    }
  }

  igPopFont();

  /* NTL-compatible tags are attached to every visible snake. Public tags
     come from the normal Slither skin packet; protected tags are supplied by
     the same NTL mapping service used by the browser extension. */
  ntl_tags_begin_frame();
  if (usrs->show_tags) {
    int tag_snakes_len = tdarray_length(gdata->data.snakes);
    for (int i = tag_snakes_len - 1; i >= 0; --i) {
      snake* tagged = &gdata->data.snakes[i];
      if (!tagged->iiv) continue;
      if ((usrs->ntl_stealth_mode &&
           tagged->id == gdata->data.snake_id) ||
          (usrs->mode_hide_enemy_tags[mode_index] &&
           tagged->id != gdata->data.snake_id &&
           !redraw_snake_is_teammate(tagged, usrs)))
        continue;
      float tag_alpha = tagged->alive_amt * (1.0f - tagged->dead_amt);
      if (usrs->show_ntl_tags && tagged->ntl_tag_id >= 0)
        ntl_tags_draw(env, tagged, tag_alpha, mww2, mhh2);
      if (usrs->show_vlither_tags && tagged->vlither_tag_id >= 0)
        vlither_tags_draw(env, tagged, tag_alpha, mww2, mhh2);
    }
  }

  usr->r->global.view[0] = gdata->data.view_xx;
  usr->r->global.view[1] = gdata->data.view_yy;
  usr->r->global.zoom = gdata->data.gsc;
  usr->r->global.grd = gdata->data.grd;
  usr->r->global.bd_radius = gdata->data.flux_grd;
  /* background_style's "Custom (Upload)" entry (17) shares the same
     variant-23 slot as the homepage's Custom (Upload) option, one file on
     disk — the 17..22 range is already used by homepage preset photos. */
  renderer_set_background_variant(
      usr->r, env->ctx,
      usrs->background_style == 17 ? 23 : usrs->background_style);
  usr->r->global.bd_color[0] = usrs->bd_color[0];
  usr->r->global.bd_color[1] = usrs->bd_color[1];
  usr->r->global.bd_color[2] = usrs->bd_color[2];
  usr->r->global.bg_scale = mode->bg_scale;
  usr->r->global.bg_blur = 0.0f;
  usr->r->global.bg_color[0] = 1.0f;
  usr->r->global.bg_color[1] = 1.0f;
  usr->r->global.bg_color[2] = 1.0f;
  usr->r->global.bg_opacity = 1;
  usr->r->global.bd_opacity = 0.8f;
  usr->r->global.minimap_data_size = gdata->data.mmsz;

  if (mode->show_crosshair) {
    /* Local-player head dot. The old show_crosshair setting name is retained
       internally so existing settings files keep working, but the marker is
       now a small configurable-colour dot fixed to the player's own snake
       head. It is never drawn for remote snakes and does not depend on the
       Android touch cursor. */
    snake *me = get_snake(gdata, gdata->data.snake_id);
    if (me && me->iiv && !me->dead) {
      float head_alpha = me->alive_amt * (1.0f - me->dead_amt);
      if (head_alpha > 0.01f) {
        /* Put the marker right at the front tip of the head, just proud of
           the surface, in the snake's own facing direction. The head is a
           capsule of radius 14.5*sc centred on hx,hy (same radius the body
           segments use — see lsz = 29*ssc a bit further down), so an offset
           of exactly that radius lands the dot on the surface; a hair more
           (15.5*sc) floats it just above/ahead of the tip instead of
           sitting on top of it. */
        float forehead_offset = 15.5f * me->sc;
        float dot_wx = me->xx + me->fx + cosf(me->ehang) * forehead_offset;
        float dot_wy = me->yy + me->fy + sinf(me->ehang) * forehead_offset;
        float head_x = mww2 + (dot_wx - gdata->data.view_xx) *
                                  gdata->data.gsc;
        float head_y = mhh2 + (dot_wy - gdata->data.view_yy) *
                                  gdata->data.gsc;
        /* World-space size (like Frontier's pointer dot): the dot is a small
           fixed fraction of the snake's own thickness, so it scales with
           zoom the same way the snake does — bigger in raw pixels when
           zoomed in, but still reading as a small dot next to the now-much
           bigger head, and smaller (but present) when zoomed out. Clamped so
           it never vanishes at max zoom-out or balloons at max zoom-in. */
        float head_dot_radius_px =
            GLM_MAX(1.5f, GLM_MIN(2.2f * me->sc * gdata->data.gsc, 9.0f));
        ImDrawList_AddCircleFilled(
            igGetWindowDrawList(), (ImVec2){head_x, head_y}, head_dot_radius_px,
            igColorConvertFloat4ToU32(
                (ImVec4){usrs->head_dot_color[0], usrs->head_dot_color[1],
                         usrs->head_dot_color[2], head_alpha}),
            16);
      }
    }
  }

  usr->r->global.bg_color[0] = usr->r->global.bg_color[1] =
      usr->r->global.bg_color[2] = mode->show_background;
  usr->r->fdr->pipeline_idx = (mode->food_type >= 0 && mode->food_type <= 2) ? mode->food_type : 0;

  lerp_minimap_float(gdata->data.mm_data_follow, gdata->data.mm_data,
                     gdata->data.mmsz, 0.05f * gdata->data.vfr);

  minimap_float_to_u8(gdata->data.mm_data_follow, usr->r->mmr->minimap,
                      gdata->data.mmsz);

  gdata->data.frames++;

  if (gdata->data.fps_etm >= 1000) {
    gdata->data.fps = gdata->data.frames;
    gdata->data.frames = 0;
    gdata->data.fps_ltm = gdata->data.ctm;
  }

  /* gdata->data.ping / ping_peak are maintained per pong by
     ping_mark_received() (NTL ping system); this only smooths the colour. */
  gdata->data.ping_follow = glm_lerp(
      gdata->data.ping_follow,
      GLM_MAX(GOOD_PING, GLM_MIN((float)gdata->data.ping, BAD_PING)),
      0.1f * gdata->data.vfr);
}
