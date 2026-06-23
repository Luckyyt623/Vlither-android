#include "title_screen.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include "../network/server.h"
#include "../user.h"
#include "ntl_panel.h"

void ui_title_screen_init(tenv* env) {
  ui_ntl_panel_init();
}

void ui_title_screen(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  ImGuiIO* io = igGetIO_Nil();
  game_data* gdata = &usr->gdata;
  
  // version
  char version_str[16] = {0};
  sprintf(version_str, "v%s", APP_VERSION);
  ImVec2 vtxtsz; igCalcTextSize(&vtxtsz, version_str, NULL, false, -1);
  igSetCursorPosX(ctx->size[0] - vtxtsz.x - style->WindowPadding.x);
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  igTextColored((ImVec4){0.168f, 0.668f, 0.375f, 1}, version_str);
  igPopFont();

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  usr->r->global.bg_opacity = 0;
  usr->r->global.bd_opacity = 0;
  usr->r->global.minimap_opacity = 0;

  float frame_height = igGetFrameHeight();

  float logo_size = 400;
  float logo_gap = 5;

  igPushFont(usr->imgui_data.mono_font[usrs->ui_font_size],
             usr->imgui_data.mono_font[usrs->ui_font_size]->LegacySize);
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f -
                  (igGetFrameHeight() - style->ItemSpacing.x) * 3);
  int tot_sec = (int)usrs->play_time;
  int hours = tot_sec / 3600;
  int minutes = (tot_sec % 3600) / 60;
  int seconds = tot_sec % 60;
  igTextColored((ImVec4){1, 1, 1, 0.5f}, "\ue99e");
  igSameLine(0, -1);
  igTextColored((ImVec4){1, 1, 1, 0.5f}, "%d", usrs->score);
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f -
                  (igGetFrameHeight() - style->ItemSpacing.x) * 2);
  igTextColored((ImVec4){1, 1, 1, 0.5f}, "\ueaeb");
  igSameLine(0, -1);
  igTextColored((ImVec4){1, 1, 1, 0.5f}, "%d", usrs->kills);
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f -
                  (igGetFrameHeight() - style->ItemSpacing.x));
  igTextColored((ImVec4){1, 1, 1, 0.6}, "\ue952");
  igSameLine(0, -1);
  igTextColored((ImVec4){1, 1, 1, 0.6}, "%02d:%02d:%02d", hours, minutes,
                seconds);
  igPopFont();

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y);
  igPushItemWidth(logo_size);
  igPushStyleColor_Vec4(ImGuiCol_FrameBg,
                        (ImVec4){0.297f, 0.265f, 0.484f, 1.0f});
  igInputTextWithHint("##nickname_input", "Nickname", usrs->nickname,
                      MAX_NICKNAME_LEN + 1, ImGuiInputTextFlags_None, NULL,
                      NULL);
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 2 +
                  frame_height);
  igInputTextWithHint("##ipv4_input", "IPv4:Port", usrs->ipv4, MAX_IPV4_LEN + 1,
                      ImGuiInputTextFlags_None, NULL, NULL);
  igPopStyleColor(1);
  igPopItemWidth();
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 3 +
                  frame_height * 2);

  if (igButton("\uea1c Play", (ImVec2){logo_size})) {
    usr->gdata.conn = CONNECTING;
    usr->gdata.curr_screen = PLAYING;
    glfwSetTime(0);
    server_connect(env);
  }
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 4 +
                  frame_height * 3);
  if (igButton("\ue90c Skin editor",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2}))
    usr->gdata.curr_screen = SKIN_EDITOR;
  igSameLine(0, -1);
  if (igButton("\ue991 Settings",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usr->gdata.curr_screen = SETTINGS;
  }

  /* ── NTL Teammates button ─────────────────────────────────────────── */
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 5 +
                  frame_height * 4);
  igPushStyleColor_Vec4(ImGuiCol_Button,
      (ImVec4){0.04f, 0.22f, 0.40f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
      (ImVec4){0.07f, 0.34f, 0.58f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
      (ImVec4){0.02f, 0.16f, 0.30f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_Text,
      (ImVec4){0.20f, 0.90f, 1.0f, 1.0f});
  if (igButton("\ue9c5 NTL Teammates", (ImVec2){logo_size})) {
    ui_ntl_panel_open();
  }
  igPopStyleColor(4);

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 6 +
                  frame_height * 5);
  if (igButton("\ue9b6 Quit", (ImVec2){logo_size})) {
    env->config.running = false;
    save_user_settings(usrs);
  }

#ifdef ANDROID
  /* ── Crash warning note ─────────────────────────────────────────── */
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 7 +
                  frame_height * 6);
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){1.0f, 0.75f, 0.25f, 0.85f});
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  igSetNextItemWidth(logo_size);
  igTextWrapped(
    "Note: The game can crash while switching Joystick and Trackpad. "
    "After crashing, open the app again.");
  igPopFont();
  igPopStyleColor(1);

  /* ── Control mode toggle: Joystick / Trackpad ───────────────────── */
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);

  /* Left half: Joystick (highlighted when active) */
  if (!usrs->ctrl_mode_trackpad) {
    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.168f, 0.468f, 0.768f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.268f, 0.568f, 0.868f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,  (ImVec4){0.068f, 0.368f, 0.668f, 1.0f});
  }
  if (igButton("Joystick",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usrs->ctrl_mode_trackpad = false;
    save_user_settings(usrs);
  }
  if (!usrs->ctrl_mode_trackpad) igPopStyleColor(3);

  igSameLine(0, -1);

  /* Right half: Trackpad (highlighted when active) */
  if (usrs->ctrl_mode_trackpad) {
    igPushStyleColor_Vec4(ImGuiCol_Button,        (ImVec4){0.068f, 0.568f, 0.368f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.168f, 0.668f, 0.468f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,  (ImVec4){0.0f,   0.468f, 0.268f, 1.0f});
  }
  if (igButton("Trackpad",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usrs->ctrl_mode_trackpad = true;
    save_user_settings(usrs);
  }
  if (usrs->ctrl_mode_trackpad) igPopStyleColor(3);
#endif /* ANDROID */

  igPopFont();

  /* NTL panel — renders as full-screen overlay when open */
  ui_ntl_panel(env);
}

void ui_title_screen_destroy(tenv* env) {}
