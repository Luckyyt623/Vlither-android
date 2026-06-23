/*
 * ntl_panel.c  –  NTL Teammate Sync settings panel for Vlither Android
 *
 * Opened from the home screen by tapping the "NTL" button.
 * Layout:
 *   Left half  : Scrollable NTL settings (teams, chat toggle, user ID)
 *   Right half : Dim overlay — tap anywhere on it to close the panel
 *
 * Two action buttons at bottom of left panel:
 *   [OK]    : saves settings and closes
 *   [Reset] : clears all NTL data, regenerates user ID
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

static bool   s_open         = false;
static float  s_anim         = 0.0f;   /* 0..1 slide-in progress */
static bool   s_add_mode     = false;  /* sub-form for adding a new team */
static int    s_selected_idx = -1;     /* highlighted team in the list */

/* Scratch buffers for the "add team" sub-form */
static char s_new_name   [33] = {0};
static char s_new_team_id[33] = {0};
static char s_new_auth   [65] = {0};

/* Confirmation popups */
static bool s_confirm_reset  = false;

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void gen_user_id(char* out8) {
  static const char hex[] = "0123456789abcdef";
  srand((unsigned)time(NULL));
  for (int i = 0; i < 8; i++) out8[i] = hex[rand() % 16];
  out8[8] = '\0';
}

static void do_reset(user_settings* usrs) {
  memset(usrs->ntl_teams,   0, sizeof(usrs->ntl_teams));
  usrs->ntl_team_count      = 0;
  usrs->ntl_active_team_idx = -1;
  memset(usrs->ntl_user_id, 0, sizeof(usrs->ntl_user_id));
  gen_user_id(usrs->ntl_user_id);
  usrs->show_chat_hud            = true;
  usrs->show_online_players_hud  = false;
  usrs->show_player_details_hud  = false;
  s_selected_idx = -1;
  s_add_mode     = false;
}

/* ── Public API ─────────────────────────────────────────────────────── */

void ui_ntl_panel_init(void) {
  s_open  = false;
  s_anim  = 0.0f;
}

void ui_ntl_panel_open(void) {
  s_open         = true;
  s_anim         = 0.0f;
  s_add_mode     = false;
  s_confirm_reset = false;
  memset(s_new_name,    0, sizeof(s_new_name));
  memset(s_new_team_id, 0, sizeof(s_new_team_id));
  memset(s_new_auth,    0, sizeof(s_new_auth));
}

/* ── Per-frame render ─────────────────────────────────────────────────
 * Call inside the main ImGui overlay window (full-screen NoDecoration).  */
void ui_ntl_panel(tenv* env) {
  if (!s_open) return;

  tuser_data*    usr  = env->usr;
  tcontext*      ctx  = env->ctx;
  user_settings* usrs = &usr->usrs;

  float sw = ctx->size[0];
  float sh = ctx->size[1];

  /* Animate slide-in */
  float speed = igGetIO_Nil()->DeltaTime * 6.0f;
  if (s_anim < 1.0f) { s_anim += speed; if (s_anim > 1.0f) s_anim = 1.0f; }

  /* Ease-out cubic: t^2*(3-2t) */
  float t = s_anim * s_anim * (3.0f - 2.0f * s_anim);

  float panel_w  = sw * 0.52f;
  float panel_x0 = -panel_w + panel_w * t;  /* starts off-screen */
  float panel_x1 = panel_x0 + panel_w;

  /* ── Full-screen overlay window ─────────────────────────────────────
   * Sits on top of everything else.  Passes through events only to itself. */
  igSetNextWindowPos((ImVec2){0, 0}, ImGuiCond_Always, (ImVec2){0, 0});
  igSetNextWindowSize((ImVec2){sw, sh}, ImGuiCond_Always);
  igSetNextWindowBgAlpha(0.0f);

  ImGuiWindowFlags of =
      ImGuiWindowFlags_NoDecoration  | ImGuiWindowFlags_NoMove  |
      ImGuiWindowFlags_NoScrollbar   | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus;

  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0, 0});

  if (!igBegin("##ntl_overlay", NULL, of)) {
    igEnd();
    igPopStyleVar(1);
    return;
  }

  ImDrawList* dl = igGetWindowDrawList();

  /* ── Right-side dim ──────────────────────────────────────────────── */
  float dim_alpha = 0.55f * t;
  ImDrawList_AddRectFilled(dl,
      (ImVec2){panel_x1, 0}, (ImVec2){sw, sh},
      IM_COL32(0, 0, 0, (int)(dim_alpha * 255)), 0, 0);

  /* Right-side tap closes the panel */
  ImGuiIO* io = igGetIO_Nil();
  if (io && igIsMouseClicked_Bool(0, false)) {
    float mx = io->MousePos.x, my = io->MousePos.y;
    if (mx > panel_x1) {
      /* Animate out */
      s_anim = 0.0f;
      s_open = false;
      igEnd();
      igPopStyleVar(1);
      return;
    }
  }

  /* ── Left panel background ─────────────────────────────────────────
   * Gradient-style: dark blue-black with subtle cyan border on the right. */
  ImDrawList_AddRectFilled(dl,
      (ImVec2){panel_x0, 0}, (ImVec2){panel_x1, sh},
      IM_COL32(10, 14, 24, 245), 0, 0);
  /* Right border glow line */
  ImDrawList_AddLine(dl,
      (ImVec2){panel_x1, 0}, (ImVec2){panel_x1, sh},
      IM_COL32(30, 180, 200, 200), 1.8f);
  /* Header bar */
  ImDrawList_AddRectFilled(dl,
      (ImVec2){panel_x0, 0}, (ImVec2){panel_x1, sh * 0.09f},
      IM_COL32(10, 60, 90, 240), 0, 0);
  /* Header accent line */
  ImDrawList_AddLine(dl,
      (ImVec2){panel_x0, sh * 0.09f}, (ImVec2){panel_x1, sh * 0.09f},
      IM_COL32(0, 200, 220, 180), 2.0f);

  /* ── Header text ─────────────────────────────────────────────────── */
  igPushFont(usr->imgui_data.mono_font[FONT_SIZE_LARGE],
             usr->imgui_data.mono_font[FONT_SIZE_LARGE]->LegacySize);
  float hfsz = sh * 0.038f;
  float hsc  = hfsz / igGetFontSize();
  const char* htitle = "NTL Teammates";
  ImVec2 htsz; igCalcTextSize(&htsz, htitle, NULL, false, -1);
  ImDrawList_AddText_FontPtr(dl, igGetFont(), hfsz,
      (ImVec2){panel_x0 + 18, sh * 0.09f * 0.5f - hfsz * 0.5f},
      IM_COL32(0, 210, 240, 245), htitle, NULL, 0, NULL);

  /* Status indicator dot */
  int ntl_cnt = 0; ntl_get_players(&ntl_cnt);
  ImU32 status_col = ntl_cnt > 0
      ? IM_COL32(50, 255, 120, 230)
      : IM_COL32(180, 180, 180, 130);
  ImDrawList_AddCircleFilled(dl,
      (ImVec2){panel_x1 - 22, sh * 0.045f}, 6.0f, status_col, 16);
  const char* status_txt = ntl_cnt > 0 ? "LIVE" : "IDLE";
  float sfsz = sh * 0.022f;
  float ssc  = sfsz / igGetFontSize();
  ImVec2 ssz; igCalcTextSize(&ssz, status_txt, NULL, false, -1);
  ImDrawList_AddText_FontPtr(dl, igGetFont(), sfsz,
      (ImVec2){panel_x1 - 22 - ssz.x * ssc - 6, sh * 0.045f - sfsz * 0.5f},
      status_col, status_txt, NULL, 0, NULL);
  igPopFont();

  /* ── Scrollable content child window ───────────────────────────────
   * We need actual ImGui widgets here, so we position an ImGui child.  */
  float btn_area_h = sh * 0.10f;    /* space reserved at bottom for OK/Reset */
  float content_y  = sh * 0.09f;
  float content_h  = sh - content_y - btn_area_h;

  igSetCursorPos((ImVec2){panel_x0, content_y});
  igBeginChild_Str("##ntl_scroll",
      (ImVec2){panel_w, content_h},
      false,
      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

  float inner_pad = 14.0f;
  igPushItemWidth(panel_w - inner_pad * 2);

  /* ── Shared style ─────────────────────────────────────────────────── */
  igPushStyleColor_Vec4(ImGuiCol_FrameBg,        (ImVec4){0.08f,0.20f,0.30f,0.90f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered, (ImVec4){0.10f,0.30f,0.42f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgActive,  (ImVec4){0.12f,0.36f,0.50f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_Button,         (ImVec4){0.06f,0.25f,0.40f,0.90f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,  (ImVec4){0.10f,0.38f,0.55f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,   (ImVec4){0.04f,0.18f,0.32f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_CheckMark,      (ImVec4){0.0f, 0.85f,0.95f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_Header,         (ImVec4){0.04f,0.20f,0.40f,0.90f});
  igPushStyleColor_Vec4(ImGuiCol_HeaderHovered,  (ImVec4){0.07f,0.30f,0.50f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_HeaderActive,   (ImVec4){0.10f,0.38f,0.60f,1.0f});
  /* 10 colour pushes */

  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_MEDIUM],
             usr->imgui_data.regular_font[FONT_SIZE_MEDIUM]->LegacySize);

  igDummy((ImVec2){0, 6});

  /* ── Section: Active Team ─────────────────────────────────────────── */
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
  igSetCursorPosX(inner_pad);
  igText("Active Team");
  igPopStyleColor(1);
  igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
  igSetCursorPosX(inner_pad);
  igSeparator();
  igPopStyleColor(1);
  igDummy((ImVec2){0, 4});

  if (usrs->ntl_team_count == 0) {
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.5f,0.5f,0.5f,0.8f});
    igTextWrapped("No teams yet. Tap \"+ Add Team\" below to add one.");
    igPopStyleColor(1);
  } else {
    /* Team list: selectable items */
    float list_h = fminf((float)usrs->ntl_team_count * 52.0f + 8.0f, sh * 0.26f);
    igSetCursorPosX(inner_pad);
    igBeginChild_Str("##team_list",
        (ImVec2){panel_w - inner_pad * 2, list_h},
        true, ImGuiWindowFlags_None);

    for (int ti = 0; ti < usrs->ntl_team_count; ti++) {
      bool is_active   = (ti == usrs->ntl_active_team_idx);
      bool is_selected = (ti == s_selected_idx);

      /* Row background */
      ImVec4 row_col = is_active
          ? (ImVec4){0.05f,0.30f,0.15f,0.90f}
          : (ImVec4){0.05f,0.12f,0.22f,0.80f};
      igPushStyleColor_Vec4(ImGuiCol_Header,        row_col);
      igPushStyleColor_Vec4(ImGuiCol_HeaderHovered, (ImVec4){0.07f,0.35f,0.55f,1.0f});

      char row_id[64];
      snprintf(row_id, sizeof(row_id), "##team_row_%d", ti);

      ImVec2 cur; igGetCursorPos(&cur);
      bool clicked = igSelectable_BoolPtr(row_id, &is_selected,
          ImGuiSelectableFlags_None, (ImVec2){0, 48.0f});
      igPopStyleColor(2);

      if (clicked) s_selected_idx = (s_selected_idx == ti) ? -1 : ti;

      /* Draw content over the selectable */
      ImVec2 after; igGetCursorPos(&after);
      igSetCursorPos((ImVec2){cur.x + 8, cur.y + 6});

      /* Active badge */
      if (is_active) {
        igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.20f,1.0f,0.55f,1.0f});
        igText("\xe2\x96\xb6"); /* ► */
        igPopStyleColor(1);
        igSameLine(0, 4);
      } else {
        igDummy((ImVec2){16, 1}); igSameLine(0, 4);
      }

      /* Team name */
      igPushStyleColor_Vec4(ImGuiCol_Text,
          is_active
            ? (ImVec4){0.40f, 1.0f, 0.70f, 1.0f}
            : (ImVec4){0.85f, 0.92f, 1.0f,  1.0f});
      igText("%s", usrs->ntl_teams[ti].name[0]
               ? usrs->ntl_teams[ti].name : "(unnamed)");
      igPopStyleColor(1);

      /* Sub-text: team_id */
      igSetCursorPos((ImVec2){cur.x + 28, cur.y + 26});
      igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.4f,0.6f,0.8f,0.85f});
      igPushFont(usr->imgui_data.mono_font[FONT_SIZE_SMALL],
                 usr->imgui_data.mono_font[FONT_SIZE_SMALL]->LegacySize);
      igText("ID: %s", usrs->ntl_teams[ti].team_id[0]
                         ? usrs->ntl_teams[ti].team_id : "—");
      igPopFont();
      igPopStyleColor(1);

      igSetCursorPos(after);
    }
    igEndChild(); /* team_list */

    /* Row actions for selected team */
    if (s_selected_idx >= 0 && s_selected_idx < usrs->ntl_team_count) {
      igDummy((ImVec2){0, 4});
      igSetCursorPosX(inner_pad);

      float aw = (panel_w - inner_pad * 2 - 8) / 3.0f;

      /* Set Active */
      bool already_active = (s_selected_idx == usrs->ntl_active_team_idx);
      if (already_active) {
        igPushStyleColor_Vec4(ImGuiCol_Button,
            (ImVec4){0.04f,0.28f,0.16f,0.95f});
        igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
            (ImVec4){0.06f,0.38f,0.22f,1.0f});
      }
      if (igButton(already_active ? "\xe2\x9c\x93 Active" : "Set Active",
                   (ImVec2){aw, 0})) {
        usrs->ntl_active_team_idx = already_active ? -1 : s_selected_idx;
      }
      if (already_active) igPopStyleColor(2);

      igSameLine(0, 4);

      /* Edit — load into add-form */
      if (igButton("Edit", (ImVec2){aw, 0})) {
        ntl_team_profile* tp = &usrs->ntl_teams[s_selected_idx];
        strncpy(s_new_name,    tp->name,    sizeof(s_new_name)-1);
        strncpy(s_new_team_id, tp->team_id, sizeof(s_new_team_id)-1);
        strncpy(s_new_auth,    tp->auth_key,sizeof(s_new_auth)-1);
        s_add_mode = true;
        /* delete the slot so it gets re-added on Save */
        int del = s_selected_idx;
        for (int i=del; i<usrs->ntl_team_count-1; i++)
          usrs->ntl_teams[i] = usrs->ntl_teams[i+1];
        usrs->ntl_team_count--;
        if (usrs->ntl_active_team_idx == del)
          usrs->ntl_active_team_idx = -1;
        else if (usrs->ntl_active_team_idx > del)
          usrs->ntl_active_team_idx--;
        s_selected_idx = -1;
      }

      igSameLine(0, 4);

      /* Delete */
      igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.36f,0.06f,0.06f,0.90f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.55f,0.10f,0.10f,1.0f});
      if (igButton("Delete", (ImVec2){aw, 0})) {
        int del = s_selected_idx;
        for (int i=del; i<usrs->ntl_team_count-1; i++)
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

  /* ── Add Team sub-form ───────────────────────────────────────────── */
  if (!s_add_mode) {
    igSetCursorPosX(inner_pad);
    if (usrs->ntl_team_count < 12) {
      igPushStyleColor_Vec4(ImGuiCol_Button,
          (ImVec4){0.06f,0.32f,0.14f,0.90f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
          (ImVec4){0.10f,0.45f,0.20f,1.0f});
      if (igButton("+ Add Team", (ImVec2){panel_w - inner_pad * 2, 0})) {
        memset(s_new_name,    0, sizeof(s_new_name));
        memset(s_new_team_id, 0, sizeof(s_new_team_id));
        memset(s_new_auth,    0, sizeof(s_new_auth));
        s_add_mode = true;
      }
      igPopStyleColor(2);
    } else {
      igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.5f,0.5f,0.5f,0.7f});
      igSetCursorPosX(inner_pad);
      igText("Max 12 teams reached.");
      igPopStyleColor(1);
    }
  } else {
    /* ── Add/Edit sub-form ────────────────────────────────────────── */
    igDummy((ImVec2){0, 4});
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
    igSetCursorPosX(inner_pad);
    igText(s_new_name[0] || s_new_team_id[0] ? "Edit Team" : "Add Team");
    igPopStyleColor(1);
    igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
    igSetCursorPosX(inner_pad); igSeparator();
    igPopStyleColor(1);
    igDummy((ImVec2){0, 4});

    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.6f,0.8f,0.9f,0.9f});
    igText("Team Name:");
    igPopStyleColor(1);
    igSetCursorPosX(inner_pad);
    igInputTextWithHint("##new_name", "e.g.  My Squad",
        s_new_name, sizeof(s_new_name), 0, NULL, NULL);

    igDummy((ImVec2){0, 4});
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.6f,0.8f,0.9f,0.9f});
    igText("Team ID:");
    igPopStyleColor(1);
    igSetCursorPosX(inner_pad);
    igInputTextWithHint("##new_tid", "e.g.  myteam",
        s_new_team_id, sizeof(s_new_team_id), 0, NULL, NULL);

    igDummy((ImVec2){0, 4});
    igSetCursorPosX(inner_pad);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.6f,0.8f,0.9f,0.9f});
    igText("Auth Key:");
    igPopStyleColor(1);
    igSetCursorPosX(inner_pad);
    igInputTextWithHint("##new_auth", "Auth key from ntl-slither.com",
        s_new_auth, sizeof(s_new_auth),
        ImGuiInputTextFlags_Password, NULL, NULL);

    igDummy((ImVec2){0, 8});
    igSetCursorPosX(inner_pad);
    float hw = (panel_w - inner_pad * 2 - 8) * 0.5f;

    igPushStyleColor_Vec4(ImGuiCol_Button,
        (ImVec4){0.06f,0.32f,0.14f,0.90f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
        (ImVec4){0.10f,0.46f,0.22f,1.0f});
    if (igButton("Save Team", (ImVec2){hw, 0})) {
      if (s_new_team_id[0] && s_new_auth[0] &&
          usrs->ntl_team_count < 12) {
        ntl_team_profile* tp = &usrs->ntl_teams[usrs->ntl_team_count];
        strncpy(tp->name,    s_new_name,    sizeof(tp->name)-1);
        strncpy(tp->team_id, s_new_team_id, sizeof(tp->team_id)-1);
        strncpy(tp->auth_key,s_new_auth,    sizeof(tp->auth_key)-1);
        usrs->ntl_team_count++;
        s_selected_idx = usrs->ntl_team_count - 1;
      }
      s_add_mode = false;
    }
    igPopStyleColor(2);

    igSameLine(0, 8);

    igPushStyleColor_Vec4(ImGuiCol_Button,
        (ImVec4){0.22f,0.06f,0.06f,0.90f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
        (ImVec4){0.38f,0.10f,0.10f,1.0f});
    if (igButton("Cancel", (ImVec2){hw, 0})) {
      s_add_mode = false;
    }
    igPopStyleColor(2);
  }

  igDummy((ImVec2){0, 16});

  /* ── Section: Chat ────────────────────────────────────────────────── */
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
  igSetCursorPosX(inner_pad);
  igText("In-Game Chat");
  igPopStyleColor(1);
  igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
  igSetCursorPosX(inner_pad); igSeparator();
  igPopStyleColor(1);
  igDummy((ImVec2){0, 6});

  igSetCursorPosX(inner_pad);
  igCheckbox("Show Chat HUD", &usrs->show_chat_hud);
  igSetCursorPosX(inner_pad);
  igCheckbox("Show online players bar", &usrs->show_online_players_hud);
  igSetCursorPosX(inner_pad);
  igCheckbox("Show teammate details", &usrs->show_player_details_hud);

  igDummy((ImVec2){0, 16});

  /* ── Section: Identity ────────────────────────────────────────────── */
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
  igSetCursorPosX(inner_pad);
  igText("Identity");
  igPopStyleColor(1);
  igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
  igSetCursorPosX(inner_pad); igSeparator();
  igPopStyleColor(1);
  igDummy((ImVec2){0, 6});

  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.6f,0.8f,0.9f,0.9f});
  igText("NTL User ID:");
  igPopStyleColor(1);

  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.85f,1.0f,0.85f,1.0f});
  igPushFont(usr->imgui_data.mono_font[FONT_SIZE_MEDIUM],
             usr->imgui_data.mono_font[FONT_SIZE_MEDIUM]->LegacySize);
  igText(usrs->ntl_user_id[0] ? usrs->ntl_user_id : "(not generated yet)");
  igPopFont();
  igPopStyleColor(1);

  igDummy((ImVec2){0, 4});
  igSetCursorPosX(inner_pad);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.55f,0.55f,0.65f,0.90f});
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  igTextWrapped(
    "Your unique 8-char ID is embedded in your nickname so teammates "
    "can recognise you. Changing it resets your identity on NTL.");
  igPopFont();
  igPopStyleColor(1);

  igDummy((ImVec2){0, 4});
  igSetCursorPosX(inner_pad);
  if (igButton("Regenerate ID", (ImVec2){panel_w - inner_pad * 2, 0})) {
    gen_user_id(usrs->ntl_user_id);
  }

  /* ── Online player count (live) ────────────────────────────────── */
  if (usrs->show_online_players_hud && ntl_cnt > 0) {
    igDummy((ImVec2){0, 16});
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.45f,0.85f,1.0f,1.0f});
    igSetCursorPosX(inner_pad);
    igText("Online Now (%d)", ntl_cnt);
    igPopStyleColor(1);
    igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.10f,0.40f,0.60f,0.70f});
    igSetCursorPosX(inner_pad); igSeparator();
    igPopStyleColor(1);
    igDummy((ImVec2){0, 4});

    ntl_player* players = ntl_get_players(&ntl_cnt);
    float list2_h = fminf((float)ntl_cnt * 36.0f + 8.0f, sh * 0.22f);
    igSetCursorPosX(inner_pad);
    igBeginChild_Str("##online_list",
        (ImVec2){panel_w - inner_pad * 2, list2_h},
        true, ImGuiWindowFlags_None);

    igPushFont(usr->imgui_data.mono_font[FONT_SIZE_SMALL],
               usr->imgui_data.mono_font[FONT_SIZE_SMALL]->LegacySize);
    for (int pi = 0; pi < ntl_cnt; pi++) {
      /* Strip 8-char hex prefix from nickname */
      const char* nn = players[pi].nickname;
      if (strlen(nn) >= 8) { bool h=true; for(int c2=0;c2<8;c2++){char cc=nn[c2];if(!((cc>='0'&&cc<='9')||(cc>='a'&&cc<='f')||(cc>='A'&&cc<='F'))){h=false;break;}} if(h)nn+=8; }

      ImVec4 ncol = players[pi].is_bot
          ? (ImVec4){0.6f,0.6f,0.6f,0.7f}
          : (ImVec4){0.2f,1.0f,0.7f,1.0f};
      igPushStyleColor_Vec4(ImGuiCol_Text, ncol);
      igText("\xe2\x98\x85 %-18s  %s pts",
             nn[0] ? nn : "(anon)", players[pi].score);
      igPopStyleColor(1);
    }
    igPopFont();
    igEndChild();
  }

  igDummy((ImVec2){0, 10});

  igPopFont();
  igPopStyleColor(10); /* 10 colour pushes from shared style */
  igPopItemWidth();
  igEndChild(); /* ntl_scroll */

  /* ── Bottom action buttons ────────────────────────────────────────
   * Positioned at absolute bottom of the left panel.                 */
  float btn_y  = sh - btn_area_h;
  float btn_h  = btn_area_h * 0.52f;
  float btn_w2 = (panel_w - inner_pad * 2 - 12) * 0.5f;

  /* Separator */
  ImDrawList_AddLine(dl,
      (ImVec2){panel_x0, btn_y}, (ImVec2){panel_x1, btn_y},
      IM_COL32(0, 170, 200, 120), 1.0f);

  /* ── OK button ─────────────────────────────────────────────────── */
  igSetCursorPos((ImVec2){panel_x0 + inner_pad, btn_y + (btn_area_h - btn_h) * 0.5f});
  igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.06f,0.36f,0.20f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.10f,0.52f,0.30f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,  (ImVec4){0.04f,0.26f,0.14f,1.0f});
  igPushStyleVar_Vec2(ImGuiStyleVar_FrameRounding, (ImVec2){8, 8});
  if (igButton("\xe2\x9c\x93  OK", (ImVec2){btn_w2, btn_h})) {
    /* Generate user ID on first-time save */
    if (!usrs->ntl_user_id[0]) gen_user_id(usrs->ntl_user_id);
    save_user_settings(usrs);
    s_open     = false;
    s_add_mode = false;
  }
  igPopStyleVar(1);
  igPopStyleColor(3);

  igSameLine(0, 12);

  /* ── Reset button ────────────────────────────────────────────────── */
  igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.30f,0.08f,0.06f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.48f,0.14f,0.10f,1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,  (ImVec4){0.20f,0.04f,0.04f,1.0f});
  igPushStyleVar_Vec2(ImGuiStyleVar_FrameRounding, (ImVec2){8, 8});
  if (igButton("\xe2\x86\xba  Reset", (ImVec2){btn_w2, btn_h})) {
    s_confirm_reset = true;
  }
  igPopStyleVar(1);
  igPopStyleColor(3);

  /* ── Reset confirmation popup ──────────────────────────────────── */
  if (s_confirm_reset) {
    igSetNextWindowPos((ImVec2){sw * 0.5f, sh * 0.5f},
        ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
    igSetNextWindowSize((ImVec2){panel_w * 0.80f, 0}, ImGuiCond_Always);
    igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4){0.08f,0.10f,0.18f,0.98f});
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){20, 20});
    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 12.0f);
    igOpenPopup_Str("##reset_confirm", 0);

    if (igBeginPopupModal("##reset_confirm", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
      igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){1.0f,0.60f,0.30f,1.0f});
      igTextWrapped("Reset ALL NTL settings?\n\nThis clears all teams, "
                    "regenerates your user ID, and restores defaults.");
      igPopStyleColor(1);
      igDummy((ImVec2){0, 10});
      ImVec2 avail; igGetContentRegionAvail(&avail);
      float cpw = avail.x;
      if (igButton("Yes, Reset", (ImVec2){cpw * 0.48f, 0})) {
        do_reset(usrs);
        save_user_settings(usrs);
        s_confirm_reset = false;
        igCloseCurrentPopup();
      }
      igSameLine(0, cpw * 0.04f);
      if (igButton("Cancel", (ImVec2){cpw * 0.48f, 0})) {
        s_confirm_reset = false;
        igCloseCurrentPopup();
      }
      igEndPopup();
    }
    igPopStyleVar(2);
    igPopStyleColor(1);
  }

  igEnd();
  igPopStyleVar(1); /* WindowPadding */
}
