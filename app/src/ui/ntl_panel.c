/*
 * ntl_panel.c  –  NTL Teammates settings panel  (Android / Vlither)
 *
 * Architecture (fixes click issues):
 *   • All VISUALS (backgrounds, borders, header, dim) → igGetForegroundDrawList
 *     This layer is always on top of every ImGui window.
 *   • All INTERACTIVE WIDGETS (buttons, inputs, checkboxes) → a dedicated
 *     top-level ImGui window "##ntl_panel_content" that receives
 *     igSetNextWindowFocus() every frame so it always has input priority.
 *   • Right-side close detection → manual hit-test on io->MousePos.
 *
 * Homepage hiding:
 *   title_screen.c calls ui_ntl_panel_is_open() and skips its content.
 */

#ifdef ANDROID
#include "../android_glfw_shim.h"
#ifndef IM_COL32
#define IM_COL32(R,G,B,A) \
  (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0))
#endif
#endif

#include "ntl_panel.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "../user.h"
#include "../network/ntl_client.h"

/* ── Panel state ─────────────────────────────────────────────────────── */

static bool  s_open         = false;
static float s_anim         = 0.0f;   /* 0..1 slide-in */
static bool  s_add_mode     = false;
static int   s_selected_idx = -1;
static bool  s_confirm_reset = false;

static char s_new_name   [33] = {0};
static char s_new_team_id[33] = {0};
static char s_new_auth   [65] = {0};

/* ── Helpers ─────────────────────────────────────────────────────────── */

bool ui_ntl_panel_is_open(void) { return s_open; }

static void gen_user_id(char* out8) {
  static const char hex[] = "0123456789abcdef";
  srand((unsigned)time(NULL));
  for (int i = 0; i < 8; i++) out8[i] = hex[rand() % 16];
  out8[8] = '\0';
}

static void do_reset(user_settings* usrs) {
  memset(usrs->ntl_teams, 0, sizeof(usrs->ntl_teams));
  usrs->ntl_team_count      = 0;
  usrs->ntl_active_team_idx = -1;
  memset(usrs->ntl_user_id, 0, sizeof(usrs->ntl_user_id));
  gen_user_id(usrs->ntl_user_id);
  usrs->show_chat_hud           = true;
  usrs->show_online_players_hud = false;
  usrs->show_player_details_hud = false;
  s_selected_idx = -1;
  s_add_mode     = false;
}

/* ── Public API ─────────────────────────────────────────────────────── */

void ui_ntl_panel_init(void) {
  s_open  = false;
  s_anim  = 0.0f;
}

void ui_ntl_panel_open(void) {
  s_open          = true;
  s_anim          = 0.0f;
  s_add_mode      = false;
  s_confirm_reset = false;
  memset(s_new_name,    0, sizeof(s_new_name));
  memset(s_new_team_id, 0, sizeof(s_new_team_id));
  memset(s_new_auth,    0, sizeof(s_new_auth));
}

/* ── Per-frame render ────────────────────────────────────────────────── */

void ui_ntl_panel(tenv* env) {
  if (!s_open) return;

  tuser_data*    usr  = env->usr;
  tcontext*      ctx  = env->ctx;
  user_settings* usrs = &usr->usrs;

  float sw = (float)ctx->size[0];
  float sh = (float)ctx->size[1];

  /* Animate slide-in (ease-out cubic) */
  float dt    = igGetIO_Nil()->DeltaTime;
  s_anim += dt * 6.0f;
  if (s_anim > 1.0f) s_anim = 1.0f;
  float t = s_anim * s_anim * (3.0f - 2.0f * s_anim);

  float panel_w  = sw * 0.52f;
  float panel_x0 = panel_w * (t - 1.0f);   /* slides from left off-screen */
  float panel_x1 = panel_x0 + panel_w;

  /* ── Right-side tap → close ─────────────────────────────────────────
   * Check BEFORE drawing so we don't lag by one frame.                  */
  ImGuiIO* io = igGetIO_Nil();
  if (io && igIsMouseClicked_Bool(ImGuiMouseButton_Left, false)) {
    if (io->MousePos.x > panel_x1 && io->MousePos.x <= sw) {
      s_open = false;
      return;
    }
  }

  /* ═══════════════════════════════════════════════════════════════════
   * LAYER 1 – Foreground DrawList (always on top of every ImGui window)
   * Paint the dim overlay on the right and the panel background on left.
   * ═══════════════════════════════════════════════════════════════════ */
  ImDrawList* fdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());

  /* Right-side dim */
  float dim_a = (int)(0.55f * t * 255.0f);
  ImDrawList_AddRectFilled(fdl,
      (ImVec2){panel_x1, 0.0f}, (ImVec2){sw, sh},
      IM_COL32(0, 0, 0, (int)dim_a), 0, 0);

  /* Panel body */
  ImDrawList_AddRectFilled(fdl,
      (ImVec2){panel_x0, 0.0f}, (ImVec2){panel_x1, sh},
      IM_COL32(10, 14, 24, 245), 0, 0);

  /* Right border glow */
  ImDrawList_AddLine(fdl,
      (ImVec2){panel_x1, 0.0f}, (ImVec2){panel_x1, sh},
      IM_COL32(30, 180, 200, 200), 1.8f);

  /* Header bar */
  float hdr_h = sh * 0.09f;
  ImDrawList_AddRectFilled(fdl,
      (ImVec2){panel_x0, 0.0f}, (ImVec2){panel_x1, hdr_h},
      IM_COL32(10, 60, 90, 240), 0, 0);
  ImDrawList_AddLine(fdl,
      (ImVec2){panel_x0, hdr_h}, (ImVec2){panel_x1, hdr_h},
      IM_COL32(0, 200, 220, 180), 2.0f);

  /* Header title text */
  ImFont* big_font = usr->imgui_data.mono_font[FONT_SIZE_LARGE];
  float   hfsz     = sh * 0.038f;
  const char* htitle = "NTL Teammates";
  ImVec2 htsz; igCalcTextSize(&htsz, htitle, NULL, false, -1.0f);
  float hsc = hfsz / big_font->LegacySize;
  ImDrawList_AddText_FontPtr(fdl, big_font, hfsz,
      (ImVec2){panel_x0 + 18.0f, hdr_h * 0.5f - hfsz * 0.5f},
      IM_COL32(0, 210, 240, 245), htitle, NULL, 0, NULL);

  /* Status dot (live/idle) */
  int ntl_live = 0; ntl_get_players(&ntl_live);
  ImU32 dot_col = ntl_live > 0
      ? IM_COL32(50, 255, 120, 230) : IM_COL32(150, 150, 150, 130);
  ImDrawList_AddCircleFilled(fdl,
      (ImVec2){panel_x1 - 22.0f, hdr_h * 0.5f}, 6.0f, dot_col, 16);

  const char* slbl = ntl_live > 0 ? "LIVE" : "IDLE";
  ImVec2 slsz; igCalcTextSize(&slsz, slbl, NULL, false, -1.0f);
  float sfsz = sh * 0.022f;
  float ssc  = sfsz / big_font->LegacySize;
  ImDrawList_AddText_FontPtr(fdl, big_font, sfsz,
      (ImVec2){panel_x1 - 22.0f - slsz.x * ssc - 6.0f,
               hdr_h * 0.5f - sfsz * 0.5f},
      dot_col, slbl, NULL, 0, NULL);

  /* Bottom action-bar separator */
  float btn_area_h = sh * 0.10f;
  float btn_y      = sh - btn_area_h;
  ImDrawList_AddLine(fdl,
      (ImVec2){panel_x0, btn_y}, (ImVec2){panel_x1, btn_y},
      IM_COL32(0, 170, 200, 120), 1.0f);

  /* ═══════════════════════════════════════════════════════════════════
   * LAYER 2 – Interactive panel window
   * Positioned exactly over the drawn panel, focused every frame.
   * ═══════════════════════════════════════════════════════════════════ */
  igSetNextWindowPos((ImVec2){panel_x0, 0.0f},
                     ImGuiCond_Always, (ImVec2){0.0f, 0.0f});
  igSetNextWindowSize((ImVec2){panel_w, sh}, ImGuiCond_Always);
  igSetNextWindowBgAlpha(0.0f);   /* transparent — visuals drawn via foreground DL */
  igSetNextWindowFocus();         /* CRITICAL: ensures this window gets all input  */

  ImGuiWindowFlags wf =
      ImGuiWindowFlags_NoDecoration   |
      ImGuiWindowFlags_NoMove         |
      ImGuiWindowFlags_NoSavedSettings|
      ImGuiWindowFlags_NoScrollbar    |
      ImGuiWindowFlags_NoScrollWithMouse;

  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);

  if (!igBegin("##ntl_panel_content", NULL, wf)) {
    igEnd();
    igPopStyleVar(2);
    return;
  }

  /* ── Shared colour/style overrides ─────────────────────────────────── */
  igPushStyleColor_Vec4(ImGuiCol_FrameBg,        (ImVec4){0.08f,0.20f,0.30f,0.90f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered, (ImVec4){0.10f,0.30f,0.42f,1.00f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgActive,  (ImVec4){0.12f,0.36f,0.50f,1.00f});
  igPushStyleColor_Vec4(ImGuiCol_Button,         (ImVec4){0.06f,0.25f,0.40f,0.90f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.10f,0.38f,0.55f,1.00f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.04f,0.18f,0.32f,1.00f});
  igPushStyleColor_Vec4(ImGuiCol_CheckMark,      (ImVec4){0.00f,0.85f,0.95f,1.00f});
  igPushStyleColor_Vec4(ImGuiCol_Header,         (ImVec4){0.04f,0.20f,0.40f,0.90f});
  igPushStyleColor_Vec4(ImGuiCol_HeaderHovered,  (ImVec4){0.07f,0.30f,0.50f,1.00f});
  igPushStyleColor_Vec4(ImGuiCol_HeaderActive,   (ImVec4){0.10f,0.38f,0.60f,1.00f});
  igPushStyleColor_Vec4(ImGuiCol_ScrollbarBg,    (ImVec4){0.04f,0.10f,0.18f,0.60f});
  igPushStyleColor_Vec4(ImGuiCol_ScrollbarGrab,  (ImVec4){0.10f,0.40f,0.60f,0.80f});
  /* Global bright text — overrides the game's near-black default */
  igPushStyleColor_Vec4(ImGuiCol_Text,           (ImVec4){0.90f,0.94f,1.00f,1.00f});
  /* 13 colour pushes */

  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding,  6.0f);
  igPushStyleVar_Float(ImGuiStyleVar_GrabRounding,   6.0f);
  /* 2 more style pushes */

  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_REGULAR],
             usr->imgui_data.regular_font[FONT_SIZE_REGULAR]->LegacySize);

  float inner_pad   = 14.0f;
  float content_y   = hdr_h;
  float content_h   = sh - hdr_h - btn_area_h;

  igPushItemWidth(panel_w - inner_pad * 2.0f);

  /* ── Scrollable content child ─────────────────────────────────────── */
  igSetCursorPos((ImVec2){0.0f, content_y});
  igBeginChild_Str("##ntl_scroll",
      (ImVec2){panel_w, content_h},
      ImGuiChildFlags_None,
      ImGuiWindowFlags_None);

  igDummy((ImVec2){0, 8});

  /* ── Section: Active Team ──────────────────────────────────────────── */
  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
  igText("Active Team");
  igPopStyleColor(1);
  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
  igSeparator();
  igPopStyleColor(1);
  igDummy((ImVec2){0, 5});

  if (usrs->ntl_team_count == 0) {
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.5f,0.5f,0.5f,0.8f});
    igTextWrapped("No teams yet. Tap \"+ Add Team\" to add one.");
    igPopStyleColor(1);
  } else {
    /* Team list */
    float row_h   = 52.0f;
    float list_h  = fminf((float)usrs->ntl_team_count * row_h + 8.0f, sh * 0.28f);
    igSetCursorPosX(inner_pad);
    igBeginChild_Str("##team_list",
        (ImVec2){panel_w - inner_pad * 2.0f, list_h},
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_None);

    for (int ti = 0; ti < usrs->ntl_team_count; ti++) {
      bool is_active   = (ti == usrs->ntl_active_team_idx);
      bool is_selected = (ti == s_selected_idx);

      igPushStyleColor_Vec4(ImGuiCol_Header,
          is_active
            ? (ImVec4){0.04f,0.28f,0.16f,0.90f}
            : (ImVec4){0.04f,0.12f,0.22f,0.80f});
      igPushStyleColor_Vec4(ImGuiCol_HeaderHovered,
          (ImVec4){0.07f,0.34f,0.52f,1.0f});

      char row_id[32]; snprintf(row_id,sizeof(row_id),"##tr%d",ti);
      ImVec2 before; igGetCursorPos(&before);
      bool clicked = igSelectable_BoolPtr(row_id, &is_selected,
          ImGuiSelectableFlags_None, (ImVec2){0, row_h - 4.0f});
      igPopStyleColor(2);

      ImVec2 after; igGetCursorPos(&after);

      if (clicked) s_selected_idx = (s_selected_idx == ti) ? -1 : ti;

      /* Draw row content */
      igSetCursorPos((ImVec2){before.x + 8.0f, before.y + 6.0f});
      if (is_active) {
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.20f,1.0f,0.55f,1.0f});
        igText("\xe2\x96\xb6"); /* ► */
        igPopStyleColor(1);
        igSameLine(0, 4);
      } else {
        igDummy((ImVec2){18, 1}); igSameLine(0, 4);
      }
      igPushStyleColor_Vec4(ImGuiCol_Text,
          is_active
            ? (ImVec4){0.40f,1.0f,0.70f,1.0f}
            : (ImVec4){0.88f,0.94f,1.0f,1.0f});
      igText("%s", usrs->ntl_teams[ti].name[0]
                     ? usrs->ntl_teams[ti].name : "(unnamed)");
      igPopStyleColor(1);

      igSetCursorPos((ImVec2){before.x + 28.0f, before.y + 28.0f});
      igPushFont(usr->imgui_data.mono_font[FONT_SIZE_SMALL],
                 usr->imgui_data.mono_font[FONT_SIZE_SMALL]->LegacySize);
      igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.40f,0.60f,0.80f,0.85f});
      igText("ID: %.24s", usrs->ntl_teams[ti].team_id[0]
                            ? usrs->ntl_teams[ti].team_id : "—");
      igPopStyleColor(1);
      igPopFont();

      igSetCursorPos(after);
    }
    igEndChild(); /* team_list */

    /* Row actions */
    if (s_selected_idx >= 0 && s_selected_idx < usrs->ntl_team_count) {
      igDummy((ImVec2){0, 4});
      igSetCursorPosX(inner_pad);
      float aw = (panel_w - inner_pad * 2.0f - 8.0f) / 3.0f;

      /* Set Active */
      bool already = (s_selected_idx == usrs->ntl_active_team_idx);
      if (already) {
        igPushStyleColor_Vec4(ImGuiCol_Button,
            (ImVec4){0.04f,0.30f,0.18f,0.95f});
        igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
            (ImVec4){0.06f,0.42f,0.25f,1.0f});
      }
      if (igButton(already ? "\xe2\x9c\x93 Active" : "Set Active",
                   (ImVec2){aw, 0})) {
        usrs->ntl_active_team_idx = already ? -1 : s_selected_idx;
      }
      if (already) igPopStyleColor(2);

      igSameLine(0, 4);

      /* Edit */
      if (igButton("Edit", (ImVec2){aw, 0})) {
        ntl_team_profile* tp = &usrs->ntl_teams[s_selected_idx];
        strncpy(s_new_name,    tp->name,     sizeof(s_new_name)-1);
        strncpy(s_new_team_id, tp->team_id,  sizeof(s_new_team_id)-1);
        strncpy(s_new_auth,    tp->auth_key, sizeof(s_new_auth)-1);
        int del = s_selected_idx;
        for (int i = del; i < usrs->ntl_team_count-1; i++)
          usrs->ntl_teams[i] = usrs->ntl_teams[i+1];
        usrs->ntl_team_count--;
        if (usrs->ntl_active_team_idx == del)
          usrs->ntl_active_team_idx = -1;
        else if (usrs->ntl_active_team_idx > del)
          usrs->ntl_active_team_idx--;
        s_selected_idx = -1;
        s_add_mode = true;
      }

      igSameLine(0, 4);

      /* Delete */
      igPushStyleColor_Vec4(ImGuiCol_Button,
          (ImVec4){0.36f,0.06f,0.06f,0.90f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
          (ImVec4){0.55f,0.10f,0.10f,1.0f});
      if (igButton("Delete", (ImVec2){aw, 0})) {
        int del = s_selected_idx;
        for (int i = del; i < usrs->ntl_team_count-1; i++)
          usrs->ntl_teams[i] = usrs->ntl_teams[i+1];
        usrs->ntl_team_count--;
        if (usrs->ntl_active_team_idx == del)
          usrs->ntl_active_team_idx = -1;
        else if (usrs->ntl_active_team_idx > del)
          usrs->ntl_active_team_idx--;
        s_selected_idx = -1;
      }
      igPopStyleColor(2);
    }
  }

  igDummy((ImVec2){0, 10});

  /* ── Add / Edit sub-form ─────────────────────────────────────────── */
  if (!s_add_mode) {
    igSetCursorPosX(inner_pad);
    if (usrs->ntl_team_count < 12) {
      igPushStyleColor_Vec4(ImGuiCol_Button,
          (ImVec4){0.06f,0.32f,0.14f,0.90f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
          (ImVec4){0.10f,0.45f,0.20f,1.0f});
      if (igButton("+ Add Team",
                   (ImVec2){panel_w - inner_pad * 2.0f, 0})) {
        memset(s_new_name,    0, sizeof(s_new_name));
        memset(s_new_team_id, 0, sizeof(s_new_team_id));
        memset(s_new_auth,    0, sizeof(s_new_auth));
        s_add_mode = true;
      }
      igPopStyleColor(2);
    } else {
      igSetCursorPosX(inner_pad);
      igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.5f,0.5f,0.5f,0.7f});
      igText("Max 12 teams reached.");
      igPopStyleColor(1);
    }
  } else {
    /* ── Form ──────────────────────────────────────────────────────── */
    igDummy((ImVec2){0, 4});
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
    igText("Team Details");
    igPopStyleColor(1);
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
    igSeparator();
    igPopStyleColor(1);
    igDummy((ImVec2){0, 5});

    float fw = panel_w - inner_pad * 2.0f;

    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.60f,0.80f,0.90f,0.90f});
    igText("Team Name:");
    igPopStyleColor(1);
    igSetCursorPosX(inner_pad);
    igInputTextWithHint("##new_name", "e.g.  My Squad",
        s_new_name, sizeof(s_new_name), 0, NULL, NULL);

    igDummy((ImVec2){0, 5});
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.60f,0.80f,0.90f,0.90f});
    igText("Team ID (Key):");
    igPopStyleColor(1);
    igSetCursorPosX(inner_pad);
    igInputTextWithHint("##new_tid", "e.g.  dragon-G7zypFc34JdAjhfP",
        s_new_team_id, sizeof(s_new_team_id), 0, NULL, NULL);

    igDummy((ImVec2){0, 5});
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.60f,0.80f,0.90f,0.90f});
    igText("Auth Key:");
    igPopStyleColor(1);
    igSetCursorPosX(inner_pad);
    igInputTextWithHint("##new_auth", "Auth key from ntl-slither.com",
        s_new_auth, sizeof(s_new_auth),
        ImGuiInputTextFlags_Password, NULL, NULL);

    igDummy((ImVec2){0, 10});
    igSetCursorPosX(inner_pad);
    float hw = (fw - 8.0f) * 0.5f;

    igPushStyleColor_Vec4(ImGuiCol_Button,
        (ImVec4){0.06f,0.32f,0.14f,0.90f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
        (ImVec4){0.10f,0.46f,0.22f,1.0f});
    if (igButton("Save Team", (ImVec2){hw, 36.0f})) {
      if (s_new_team_id[0] && s_new_auth[0] && usrs->ntl_team_count < 12) {
        ntl_team_profile* tp = &usrs->ntl_teams[usrs->ntl_team_count];
        strncpy(tp->name,     s_new_name,    sizeof(tp->name)-1);
        strncpy(tp->team_id,  s_new_team_id, sizeof(tp->team_id)-1);
        strncpy(tp->auth_key, s_new_auth,    sizeof(tp->auth_key)-1);
        s_selected_idx = usrs->ntl_team_count;
        usrs->ntl_team_count++;
      }
      s_add_mode = false;
    }
    igPopStyleColor(2);

    igSameLine(0, 8);

    igPushStyleColor_Vec4(ImGuiCol_Button,
        (ImVec4){0.22f,0.06f,0.06f,0.90f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
        (ImVec4){0.38f,0.10f,0.10f,1.0f});
    if (igButton("Cancel", (ImVec2){hw, 36.0f}))
      s_add_mode = false;
    igPopStyleColor(2);
  }

  igDummy((ImVec2){0, 18});

  /* ── Section: Chat HUD ───────────────────────────────────────────── */
  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
  igText("In-Game Chat");
  igPopStyleColor(1);
  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
  igSeparator();
  igPopStyleColor(1);
  igDummy((ImVec2){0, 6});

  igSetCursorPosX(inner_pad);
  igCheckbox("Show Chat HUD", &usrs->show_chat_hud);
  igSetCursorPosX(inner_pad);
  igCheckbox("Show online players bar", &usrs->show_online_players_hud);
  igSetCursorPosX(inner_pad);
  igCheckbox("Show teammate details", &usrs->show_player_details_hud);

  igDummy((ImVec2){0, 18});

  /* ── Section: Identity ───────────────────────────────────────────── */
  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
  igText("Identity");
  igPopStyleColor(1);
  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
  igSeparator();
  igPopStyleColor(1);
  igDummy((ImVec2){0, 6});

  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.60f,0.80f,0.90f,0.90f});
  igText("NTL User ID:");
  igPopStyleColor(1);
  igSetCursorPosX(inner_pad);
  igPushFont(usr->imgui_data.mono_font[FONT_SIZE_REGULAR],
             usr->imgui_data.mono_font[FONT_SIZE_REGULAR]->LegacySize);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.85f,1.0f,0.85f,1.0f});
  igText(usrs->ntl_user_id[0] ? usrs->ntl_user_id : "(auto-generated)");
  igPopStyleColor(1);
  igPopFont();

  igDummy((ImVec2){0, 4});
  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.50f,0.50f,0.60f,0.85f});
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  igTextWrapped(
      "8-char hex ID embedded in your nickname so "
      "teammates can recognise you across servers.");
  igPopFont();
  igPopStyleColor(1);
  igDummy((ImVec2){0, 5});
  igSetCursorPosX(inner_pad);
  if (igButton("Regenerate ID",
               (ImVec2){panel_w - inner_pad * 2.0f, 0}))
    gen_user_id(usrs->ntl_user_id);

  /* ── Online now (live count) ─────────────────────────────────────── */
  if (usrs->show_online_players_hud && ntl_live > 0) {
    igDummy((ImVec2){0, 18});
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
    igText("Online Now (%d)", ntl_live);
    igPopStyleColor(1);
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
    igSeparator();
    igPopStyleColor(1);
    igDummy((ImVec2){0, 4});

    int live2 = 0;
    ntl_player* pl = ntl_get_players(&live2);
    float lh = fminf((float)live2 * 32.0f + 8.0f, sh * 0.22f);
    igSetCursorPosX(inner_pad);
    igBeginChild_Str("##online_list",
        (ImVec2){panel_w - inner_pad * 2.0f, lh},
        ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
    igPushFont(usr->imgui_data.mono_font[FONT_SIZE_SMALL],
               usr->imgui_data.mono_font[FONT_SIZE_SMALL]->LegacySize);
    for (int pi = 0; pi < live2; pi++) {
      const char* nn = pl[pi].nickname;
      if (strlen(nn) >= 8) {
        bool hex = true;
        for (int ci = 0; ci < 8; ci++) {
          char cc = nn[ci];
          if (!((cc>='0'&&cc<='9')||(cc>='a'&&cc<='f')||(cc>='A'&&cc<='F')))
            { hex=false; break; }
        }
        if (hex) nn += 8;
      }
      ImVec4 nc = pl[pi].is_bot
          ? (ImVec4){0.55f,0.55f,0.55f,0.70f}
          : (ImVec4){0.20f,1.00f,0.70f,1.00f};
      igPushStyleColor_Vec4(ImGuiCol_Text, nc);
      igText("\xe2\x98\x85 %-18s  %s", nn[0] ? nn : "(anon)", pl[pi].score);
      igPopStyleColor(1);
    }
    igPopFont();
    igEndChild();
  }

  igDummy((ImVec2){0, 12});
  igEndChild(); /* ntl_scroll */

  /* ── OK / Reset buttons (in the bottom action bar) ─────────────────
   * Positioned via SetCursorPos relative to the panel content window.  */
  float btn_h = btn_area_h * 0.54f;
  float bw    = (panel_w - inner_pad * 2.0f - 12.0f) * 0.5f;
  float by2   = btn_y + (btn_area_h - btn_h) * 0.5f;

  igSetCursorPos((ImVec2){inner_pad, by2});

  /* OK */
  igPushStyleColor_Vec4(ImGuiCol_Button,
      (ImVec4){0.06f,0.38f,0.20f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
      (ImVec4){0.10f,0.54f,0.30f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
      (ImVec4){0.04f,0.28f,0.14f,1.0f});
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 8.0f);
  if (igButton("\xe2\x9c\x93  OK", (ImVec2){bw, btn_h})) {
    if (!usrs->ntl_user_id[0]) gen_user_id(usrs->ntl_user_id);
    save_user_settings(usrs);
    s_open     = false;
    s_add_mode = false;
  }
  igPopStyleVar(1);
  igPopStyleColor(3);

  igSameLine(0, 12);

  /* Reset */
  igPushStyleColor_Vec4(ImGuiCol_Button,
      (ImVec4){0.32f,0.06f,0.06f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
      (ImVec4){0.50f,0.12f,0.10f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
      (ImVec4){0.22f,0.04f,0.04f,1.0f});
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 8.0f);
  if (igButton("\xe2\x86\xba  Reset", (ImVec2){bw, btn_h}))
    s_confirm_reset = true;
  igPopStyleVar(1);
  igPopStyleColor(3);

  /* ── Reset confirmation popup ─────────────────────────────────────── */
  if (s_confirm_reset) {
    igOpenPopup_Str("##rst_confirm", ImGuiPopupFlags_None);
  }
  igSetNextWindowPos((ImVec2){panel_w * 0.5f, sh * 0.5f},
      ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
  igSetNextWindowSize((ImVec2){panel_w * 0.82f, 0}, ImGuiCond_Always);
  igPushStyleColor_Vec4(ImGuiCol_PopupBg,
      (ImVec4){0.08f,0.10f,0.18f,0.98f});
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){20, 20});
  igPushStyleVar_Float(ImGuiStyleVar_PopupRounding, 12.0f);
  if (igBeginPopupModal("##rst_confirm", NULL,
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
    igPushStyleColor_Vec4(ImGuiCol_Text,
        (ImVec4){1.0f,0.60f,0.30f,1.0f});
    igTextWrapped("Reset ALL NTL settings?\n\n"
                  "Clears all teams, regenerates your user ID "
                  "and restores defaults.");
    igPopStyleColor(1);
    igDummy((ImVec2){0, 10});
    ImVec2 avail; igGetContentRegionAvail(&avail);
    float pw = avail.x;
    if (igButton("Yes, Reset", (ImVec2){pw * 0.48f, 0})) {
      do_reset(usrs);
      save_user_settings(usrs);
      s_confirm_reset = false;
      igCloseCurrentPopup();
    }
    igSameLine(0, pw * 0.04f);
    if (igButton("Cancel", (ImVec2){pw * 0.48f, 0})) {
      s_confirm_reset = false;
      igCloseCurrentPopup();
    }
    igEndPopup();
  }
  igPopStyleVar(2);
  igPopStyleColor(1);

  igPopItemWidth();
  igPopStyleVar(2);      /* FrameRounding, GrabRounding */
  igPopStyleColor(13);   /* 13 shared colours (incl. global Text) */
  igPopFont();

  igEnd(); /* ##ntl_panel_content */
  igPopStyleVar(2); /* WindowPadding, WindowBorderSize */
}
