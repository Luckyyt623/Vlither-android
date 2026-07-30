#include "settings.h"

#include "../user.h"

void ui_settings_init(tenv* env) {}

void ui_settings(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  ImGuiIO* io = igGetIO_Nil();
  game_data* gdata = &usr->gdata;

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  float frame_height = igGetFrameHeight();
  float child_window_height =
      ctx->size[1] - style->WindowPadding.y * 4 - frame_height;
#ifdef ANDROID
  const int panel_columns = 2;
  child_window_height = (child_window_height - style->ItemSpacing.y) * 0.5f;
#else
  const int panel_columns = 4;
#endif

  if (igBeginTable("settings_table", panel_columns, ImGuiTableFlags_None, (ImVec2){}, 0)) {
    igTableNextRow(ImGuiTableRowFlags_None, 0);
    igTableSetColumnIndex(0);

    igBeginChild_Str("general_settings_child_holder",
                     (ImVec2){-1, child_window_height}, ImGuiChildFlags_None,
                     ImGuiWindowFlags_None);
    igSeparatorText("General");
    if (igBeginTable("field:value", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
      igTableNextRow(ImGuiTableRowFlags_None, 0);
      igTableSetColumnIndex(0);
      igIndent(style->WindowPadding.x);
      igAlignTextToFramePadding();
      igText("VSync");
      igAlignTextToFramePadding();
      igText("FPS limit");
      igAlignTextToFramePadding();
      igText("Performance mode");
      igAlignTextToFramePadding();
      igText("Cursor size");
      igAlignTextToFramePadding();
      igText("UI font size");
      igAlignTextToFramePadding();
      igText("Stats font size");
      igAlignTextToFramePadding();
      igText("Leaderboard font size");
      igAlignTextToFramePadding();
      igText("Names font size");
      igAlignTextToFramePadding();
      igText("Show snake scores");
      igAlignTextToFramePadding();
      igText("Smooth zoom");
      igAlignTextToFramePadding();
      igText("Zoom step");
      igAlignTextToFramePadding();
      igText("Border color");
      igAlignTextToFramePadding();
      igText("Minimap size");
      igAlignTextToFramePadding();
      igText("Custom minimap position");
      igAlignTextToFramePadding();
      igText("Drag/resize minimap");
      igAlignTextToFramePadding();
      igText("Minimap X");
      igAlignTextToFramePadding();
      igText("Minimap Y");
      igAlignTextToFramePadding();
      igText("Reset minimap position");
      igAlignTextToFramePadding();
      igText("Instant restart");
      igAlignTextToFramePadding();
      igText("Restart with right click");
      igAlignTextToFramePadding();
      igText("Quit with middle click");
      igAlignTextToFramePadding();
      igText("Laser color");
      igAlignTextToFramePadding();
      igText("Laser thickness");
      igAlignTextToFramePadding();
      igText("Bot circle after score");
      igAlignTextToFramePadding();
      igText("Bot radius multiplier");

      igTableSetColumnIndex(1);
      if (igCheckbox("##vsync", &usrs->vsync)) {
        env->config.vsync = usrs->vsync;
        twindow_request_refresh(env->wnd);
      }
      int fps_index = 0;
      const int fps_values[] = {0, 60, 90, 120, 144};
      for (int fi = 0; fi < 5; ++fi)
        if (usrs->fps_limit == fps_values[fi]) fps_index = fi;
      igSetNextItemWidth(-1);
      if (igCombo_Str_arr("##fps limit", &fps_index,
                          (const char*[]){"Device/VSync", "60 FPS", "90 FPS",
                                          "120 FPS", "144 FPS"}, 5, -1))
        usrs->fps_limit = fps_values[fps_index];
      igCheckbox("##performance mode", &usrs->performance_mode);
      igSetNextItemWidth(-1);
      igSliderInt("##cursor size", &usrs->cursor_size, 16, 64, "%d px",
                  ImGuiSliderFlags_AlwaysClamp);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##ui font size", (int*)&usrs->ui_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##stats font size", (int*)&usrs->stats_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##leaderboard font size", (int*)&usrs->lb_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##snake name font size",
                      (int*)&usrs->snake_names_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igCheckbox("##snake scores", &usrs->snake_scores);
      igCheckbox("##smooth zoom", &usrs->smooth_zoom);
      igSetNextItemWidth(-1);
      igSliderFloat("##zoom step", &usrs->zoom_step, 0.05f, 0.5f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
      igSetNextItemWidth(-1);
      igColorEdit3("##border color", usrs->bd_color, ImGuiColorEditFlags_None);
      igSetNextItemWidth(-1);
      igSliderInt("##minimap size", &usrs->minimap_size, 96, 512, "%d px",
                  ImGuiSliderFlags_AlwaysClamp);
      igCheckbox("##minimap custom", &usrs->minimap_pos_custom);
      igCheckbox("##minimap drag", &usrs->minimap_drag_enabled);
      igBeginDisabled(!usrs->minimap_pos_custom);
      igSetNextItemWidth(-1);
      igSliderFloat("##minimap x", &usrs->minimap_rel_x, 0.0f, 1.0f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
      igSetNextItemWidth(-1);
      igSliderFloat("##minimap y", &usrs->minimap_rel_y, 0.0f, 1.0f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
      igEndDisabled();
      if (igButton("Reset##minimap", (ImVec2){-1, 0})) {
        usrs->minimap_pos_custom = false;
        usrs->minimap_rel_x = 0.84f;
        usrs->minimap_rel_y = 0.78f;
      }
      igCheckbox("##instant restart", &usrs->instant_restart);
      igCheckbox("##restart rc", &usrs->restart_rc);
      igCheckbox("##quit mc", &usrs->quit_mc);
      igSetNextItemWidth(-1);
      igColorEdit4("##laser color", usrs->laser_color,
                   ImGuiColorEditFlags_AlphaBar);
      igSetNextItemWidth(-1);
      igSliderInt("##laser thickness", &usrs->laser_thickness, 1, 4, "%d px",
                  ImGuiSliderFlags_AlwaysClamp);
                  igSetNextItemWidth(-1);
      igSliderInt("##circle after", &usrs->bot_follow_circle_score, 1000, 6000, "%d",
                  ImGuiSliderFlags_AlwaysClamp);
                  igSetNextItemWidth(-1);
      igSliderInt("##rad mult", &usrs->bot_radius_mult, 10, 40, "%dx",
                  ImGuiSliderFlags_AlwaysClamp);
      igIndent(-style->WindowPadding.x);
      igEndTable();
    }
    igSpacing();
    igTextWrapped("FPS limit is a maximum, not a forced refresh rate. Actual FPS cannot exceed your phone's active display refresh rate. Android Auto mode may keep the screen at 60 Hz; select 90/120/144 Hz in the phone's Display settings to use a matching Vlither limit.");
    igTextDisabled("VSync can also cap rendering to the current display mode.");
    igEndChild();

    igTableSetColumnIndex(1);
    igBeginChild_Str("mode_settings_child_holder",
                     (ImVec2){-1, child_window_height}, ImGuiChildFlags_None,
                     ImGuiWindowFlags_None);
    for (int i = 0; i < 2; i++) {
      igPushID_Int(i + 1);
      gameplay_mode* mode = usrs->modes + i;
      igSeparatorText(i == 0 ? "Normal mode" : "Assist mode");

      if (igBeginTable("field:value", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);
        igIndent(style->WindowPadding.x);
        igAlignTextToFramePadding();
        igText("Show crosshair");
        igAlignTextToFramePadding();
        igText("Show background");
        igAlignTextToFramePadding();
        igText("Show accessories");
        igAlignTextToFramePadding();
        igText("Show shadows");
        igAlignTextToFramePadding();
        igText("Death effect");
        igAlignTextToFramePadding();
        igText("Outline player names");
        igAlignTextToFramePadding();
        igText("Segment separation");
        igAlignTextToFramePadding();
        igText("Background scale");
        igAlignTextToFramePadding();
        igText("Render mode");
        igAlignTextToFramePadding();
        igText("Transparent skin");
        igAlignTextToFramePadding();
        igText("Skin opacity");
        igAlignTextToFramePadding();
        igText("Center line (your snake)");
        igAlignTextToFramePadding();
        igText("Boost effect");
        igAlignTextToFramePadding();
        igText("Boost effect strength");
        igAlignTextToFramePadding();
        igText("Food shader");
        igAlignTextToFramePadding();
        igText("Food scale");
        igAlignTextToFramePadding();
        igText("Food float");
        igAlignTextToFramePadding();
        igText("Food flicker");
        igAlignTextToFramePadding();
        igText("Uniform food color");

        igTableSetColumnIndex(1);
        igCheckbox("##crosshair", &mode->show_crosshair);
        igCheckbox("##bg", &mode->show_background);
        igCheckbox("##acc", &mode->show_accessories);
        igCheckbox("##shad", &mode->show_shadows);
        igCheckbox("##death effect", &mode->death_effect);
        igCheckbox("##player names outline", &mode->player_names_outline);
        igSetNextItemWidth(-1);
        igSliderFloat("##bps", &mode->qsm, 1, 4, "%.2f",
                      ImGuiSliderFlags_AlwaysClamp);
        igSetNextItemWidth(-1);
        igSliderFloat("##bgs", &mode->bg_scale, 0.05, 4, "%.2fx",
                      ImGuiSliderFlags_AlwaysClamp);
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##render mode", &mode->render_mode,
                        (const char*[]){"Texture", "Solid", "Flat"}, 3, -1);

        igCheckbox("##transparent skin", &mode->transparent_skin);
        igBeginDisabled(!mode->transparent_skin);
        int opacity_percent =
            (int)(usrs->transparent_skin_opacity[i] * 100.0f + 0.5f);
        igSetNextItemWidth(-1);
        if (igSliderInt("##skin opacity", &opacity_percent, 15, 85, "%d%%",
                        ImGuiSliderFlags_AlwaysClamp))
          usrs->transparent_skin_opacity[i] = opacity_percent / 100.0f;
        igEndDisabled();
        igCheckbox("##center line", &mode->center_line);

        igCheckbox("##boost", &mode->show_boost);
        igSameLine(0, -1);
        igBeginDisabled(!mode->show_boost);
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##boost type", &mode->boost_type,
                        (const char*[]){"Normal", "Simple"}, 2, -1);
        igSetNextItemWidth(-1);
        igSliderFloat("##boost strength", &mode->boost_strength, 0.25f, 3,
                      "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        igEndDisabled();
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##food type", &mode->food_type,
                        (const char*[]){"Solid", "Rings"}, 2, -1);
        igSetNextItemWidth(-1);
        igSliderFloat("##food scale", &mode->food_scale, 0.25f, 3, "%.2f",
                      ImGuiSliderFlags_AlwaysClamp);
        igCheckbox("##food float", &mode->food_float);
        igCheckbox("##food flicker", &mode->food_flicker);
        igCheckbox("##uniform food color", &mode->uniform_food_color);
        igSameLine(0, -1);
        igBeginDisabled(!mode->uniform_food_color);
        igSetNextItemWidth(-1);
        igColorEdit3("##fdcolor", mode->food_color, ImGuiColorEditFlags_None);
        igEndDisabled();
        igIndent(-style->WindowPadding.x);

        igEndTable();
      }
      igPopID();
    }
    igEndChild();

#ifdef ANDROID
    igTableNextRow(ImGuiTableRowFlags_None, 0);
    igTableSetColumnIndex(0);
#else
    igTableSetColumnIndex(2);
#endif
    igBeginChild_Str("hotkey_child_window", (ImVec2){-1, child_window_height},
                     ImGuiChildFlags_None, ImGuiWindowFlags_None);
    igSeparatorText("Hotkeys");
    if (igBeginTable("field:value", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
      igTableNextRow(ImGuiTableRowFlags_None, 0);
      igTableSetColumnIndex(0);
      igIndent(style->WindowPadding.x);
      for (int i = 0; i < NUM_HOTKEYS; i++) {
        hotkey* hk = usrs->hotkeys + i;
        igAlignTextToFramePadding();
        igText(hk->description);
      }
      igTableSetColumnIndex(1);

      for (int i = 0; i < NUM_HOTKEYS; i++) {
        hotkey* hk = usrs->hotkeys + i;
        igPushID_Int(i);
        igSetNextItemWidth(frame_height * 2);
        char preview_char[2] = {(char)hk->key, 0};
        if (igBeginCombo("##hotkey code", preview_char, ImGuiComboFlags_None)) {
          for (int c = 48; c < 58; c++) {
            char selectable_char[2] = {c, 0};
            bool is_in_use = false;
            for (int d = 0; d < NUM_HOTKEYS; d++) {
              if (c == usrs->hotkeys[d].key &&
                  hk->key != usrs->hotkeys[d].key) {
                is_in_use = true;
              }
            }
            if (igSelectable_Bool(selectable_char, c == hk->key,
                                  is_in_use ? ImGuiSelectableFlags_Disabled
                                            : ImGuiSelectableFlags_None,
                                  (ImVec2){})) {
              hk->key = c;
            }
          }
          for (int c = 65; c < 91; c++) {
            char selectable_char[2] = {c, 0};
            bool is_in_use = false;
            for (int d = 0; d < NUM_HOTKEYS; d++) {
              if (c == usrs->hotkeys[d].key &&
                  hk->key != usrs->hotkeys[d].key) {
                is_in_use = true;
              }
            }
            is_in_use = is_in_use || c == GLFW_KEY_M || c == GLFW_KEY_N;
            if (igSelectable_Bool(selectable_char, c == hk->key,
                                  is_in_use ? ImGuiSelectableFlags_Disabled
                                            : ImGuiSelectableFlags_None,
                                  (ImVec2){})) {
              hk->key = c;
            }
          }
          igEndCombo();
        }
        igSameLine(0, -1);
        ImVec2 rest;
        igGetContentRegionAvail(&rest);
        igSetNextItemWidth(rest.x - style->ItemInnerSpacing.x);
        if (i == HOTKEY_RESTART || i == HOTKEY_QUIT) {
          igBeginDisabled(true);
          igCombo_Str_arr("##hotkey mode", &(int){0}, (const char*[]){"Toggle"},
                          1, -1);
          igEndDisabled();
        } else {
          igCombo_Str_arr("##hotkey mode", &hk->mode,
                          (const char*[]){"Toggle", "Press and hold"}, 2, -1);
        }
        igPopID();
      }
      igIndent(-style->WindowPadding.x);

      igEndTable();
    }
    igEndChild();

#ifdef ANDROID
    igTableSetColumnIndex(1);
#else
    igTableSetColumnIndex(3);
#endif
    igBeginChild_Str("empty_col", (ImVec2){-1, child_window_height},
                     ImGuiChildFlags_None, ImGuiWindowFlags_None);
    igSeparatorText("Hotkeys");
    igEndChild();

    igEndTable();
  }

  float btn_w = ctx->size[0] * 0.25f - style->ItemSpacing.x * 2;
  float btn_h = frame_height * 1.8f;
  float col2_x = ctx->size[0] * 0.5f + style->WindowPadding.x;
  igSetCursorPosX(col2_x);
  igSetCursorPosY(ctx->size[1] - style->WindowPadding.y - btn_h * 2 - style->ItemSpacing.y);
  if (igButton("Reset", (ImVec2){btn_w, btn_h})) {
    user_settings_default(usrs);
    env->config.vsync = usrs->vsync;
    twindow_request_refresh(env->wnd);
  }
  igSetCursorPosX(col2_x);
  igSetCursorPosY(ctx->size[1] - style->WindowPadding.y - btn_h);
  if (igButton("OK", (ImVec2){btn_w, btn_h})) {
    save_user_settings(usrs);
    gdata->curr_screen = TITLE_SCREEN;
  }

  igPopFont();
}

void ui_settings_destroy(tenv* env) {}
