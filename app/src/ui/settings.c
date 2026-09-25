#include "settings.h"

#include <math.h>

#include "../user.h"
#ifdef ANDROID
#include "../android_jni.h"
#endif

#ifdef ANDROID
static bool settings_transfer_unavailable = false;
#endif

static void draw_vlither_key_selector(const char *label, int *key) {
  char preview[2] = {(char)*key, 0};
  if (!igBeginCombo(label, preview, ImGuiComboFlags_None)) return;
  for (int c = GLFW_KEY_0; c <= GLFW_KEY_9; ++c) {
    char item[2] = {(char)c, 0};
    if (igSelectable_Bool(item, *key == c, ImGuiSelectableFlags_None,
                          (ImVec2){0, 0}))
      *key = c;
  }
  for (int c = GLFW_KEY_A; c <= GLFW_KEY_Z; ++c) {
    char item[2] = {(char)c, 0};
    if (igSelectable_Bool(item, *key == c, ImGuiSelectableFlags_None,
                          (ImVec2){0, 0}))
      *key = c;
  }
  igEndCombo();
}

static void apply_mode_graphics_preset(user_settings *us, int mode_index,
                                       int preset) {
  if (!us || mode_index < 0 || mode_index > 1 || preset < 1 || preset > 4)
    return;
  us->mode_graphics_preset[mode_index] = preset;
  gameplay_mode *m = &us->modes[mode_index];
  {
    if (preset == 1) { /* Competitive */
      m->show_background = false;
      m->show_accessories = false;
      m->show_shadows = false;
      m->death_effect = false;
      m->food_flicker = false;
      m->food_float = false;
      m->food_type = 1;
      m->render_mode = 1;
      us->food_glow[mode_index] = false;
      us->snake_shadow_strength[mode_index] = 0.0f;
    } else if (preset == 2) { /* Low */
      m->show_background = false;
      m->show_accessories = false;
      m->show_shadows = false;
      m->death_effect = false;
      m->food_flicker = false;
      m->food_float = false;
      m->food_type = 0;
      m->render_mode = 2;
      us->food_glow[mode_index] = false;
      us->snake_shadow_strength[mode_index] = 0.0f;
    } else { /* Normal / High */
      m->show_background = true;
      m->show_accessories = true;
      m->show_shadows = true;
      m->death_effect = true;
      m->food_flicker = true;
      m->food_float = true;
      m->food_type = 0;
      m->render_mode = 0;
      us->food_glow[mode_index] = preset == 4;
      us->snake_shadow_strength[mode_index] = preset == 4 ? 1.35f : 1.0f;
    }
  }
  us->mode_high_visibility_skins[mode_index] = preset == 1;
  us->mode_skinless_peek[mode_index] = preset == 1;
  us->mode_hide_enemy_cosmetics[mode_index] = preset == 1;
  us->mode_hide_enemy_tags[mode_index] = preset == 1;
  us->performance_mode = us->mode_graphics_preset[0] <= 2 &&
                         us->mode_graphics_preset[1] <= 2;
}

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
  /* The action footer is pinned to the fullscreen Settings window. Reserve
     its full height before sizing the two scrollable table rows; otherwise a
     short landscape phone lets Hotkeys paint behind Create backup / Reset. */
  ImVec2 settings_window_size;
  igGetWindowSize(&settings_window_size);
  float footer_btn_h = frame_height * 1.8f;
#ifdef ANDROID
  float footer_h = footer_btn_h * 2.0f + style->ItemSpacing.y;
  float settings_table_h = settings_window_size.y -
      style->WindowPadding.y * 2.0f - footer_h -
      style->ItemSpacing.y * 2.0f;
  float child_window_height = fmaxf(
      1.0f,
      (settings_table_h - style->ItemSpacing.y) * 0.5f);
  const int panel_columns = 2;
#else
  float footer_h = footer_btn_h;
  float child_window_height = fmaxf(
      frame_height * 2.0f,
      settings_window_size.y - style->WindowPadding.y * 2.0f - footer_h -
          style->ItemSpacing.y * 2.0f);
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
      igText("Uncapped FPS");
      igAlignTextToFramePadding();
      igText("Performance mode");
      igAlignTextToFramePadding();
      igText("UI font size");
      igAlignTextToFramePadding();
      igText("Stats font size");
      igAlignTextToFramePadding();
      igText("Leaderboard font size");
      igAlignTextToFramePadding();
      igText("Leaderboard title");
      igAlignTextToFramePadding();
      igText("Title text colour");
      igAlignTextToFramePadding();
      igText("Leaderboard style");
      igAlignTextToFramePadding();
      igText("Player text colour");
      igAlignTextToFramePadding();
      igText("Names font size");
      igAlignTextToFramePadding();
      igText("Show snake scores");
      igAlignTextToFramePadding();
      igText("Chat timestamps");
      igAlignTextToFramePadding();
      igText("Stealth mode");
      igAlignTextToFramePadding();
      igText("Show tags");
      igAlignTextToFramePadding();
      igText("Show NTL tags");
      igAlignTextToFramePadding();
      igText("Show Vlither tags");
      igAlignTextToFramePadding();
      igText("Show own nickname");
      igAlignTextToFramePadding();
      igText("Background style");
      igAlignTextToFramePadding();
      igText("Smooth zoom");
      igAlignTextToFramePadding();
      igText("Zoom step");
      igAlignTextToFramePadding();
      igText("Border color");
      igAlignTextToFramePadding();
      igText("Minimap size");
      igAlignTextToFramePadding();
      igText("Clock map");
      igAlignTextToFramePadding();
      igText("Dynamic minimap");
      igAlignTextToFramePadding();
      igText("Border-distance indicator");
      igAlignTextToFramePadding();
      igText("Show my minimap name");
      igAlignTextToFramePadding();
      igText("My minimap name");
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
      igText("Instant death");
      igAlignTextToFramePadding();
      igText("Restart with right click");
      igAlignTextToFramePadding();
      igText("Quit with middle click");
#ifdef ANDROID
      igAlignTextToFramePadding();
      igText("Record gameplay in server");
      igAlignTextToFramePadding();
      igText("Screenshot after kill");
#endif
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
      const int fps_values[] = {0, 60, 90, 120, 144, 165, 240, 360};
      for (int fi = 0; fi < 8; ++fi)
        if (usrs->fps_limit == fps_values[fi]) fps_index = fi;
      igSetNextItemWidth(-1);
      if (igCombo_Str_arr("##fps limit", &fps_index,
                          (const char*[]){"No software cap", "60 FPS", "90 FPS",
                                          "120 FPS", "144 FPS", "165 FPS",
                                          "240 FPS", "360 FPS"}, 8, -1))
        usrs->fps_limit = fps_values[fps_index];
      bool uncapped_fps = !usrs->vsync && usrs->fps_limit == 0;
      if (igCheckbox("##uncapped fps", &uncapped_fps)) {
        if (uncapped_fps) {
          usrs->fps_limit = 0;
          usrs->vsync = false;
        } else {
          /* Turning the convenience toggle off returns to display-synced
             presentation. The numeric FPS limiter remains independently
             available above. */
          usrs->vsync = true;
        }
        env->config.vsync = usrs->vsync;
        twindow_request_refresh(env->wnd);
      }
#ifdef ANDROID
      if (igIsItemHovered(0)) {
        if (env->ctx && env->ctx->supports_immediate)
          igSetTooltip("Uses Vulkan IMMEDIATE present mode on this device.");
        else
          igSetTooltip("Your Vulkan driver has no safe IMMEDIATE present mode; Vlither will fall back to display-synced FIFO instead of using an unstable mode.");
      }
#endif
      igCheckbox("##performance mode", &usrs->performance_mode);
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
      igInputTextWithHint("##leaderboard title", "Vlither Leaderboard",
                          usrs->leaderboard_title,
                          sizeof usrs->leaderboard_title,
                          ImGuiInputTextFlags_None, NULL, NULL);
      igSetNextItemWidth(-1);
      igColorEdit4("##leaderboard title colour",
                   usrs->leaderboard_title_color,
                   ImGuiColorEditFlags_AlphaBar);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##leaderboard style", &usrs->ntl_leaderboard_style,
                      (const char*[]){"Snake colours", "Top-10 gradient",
                                      "Single colour"}, 3, -1);
      igBeginDisabled(usrs->ntl_leaderboard_style != 2);
      igSetNextItemWidth(-1);
      igColorEdit4("##leaderboard player colour", usrs->ntl_leaderboard_color,
                   ImGuiColorEditFlags_AlphaBar);
      igEndDisabled();
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##snake name font size",
                      (int*)&usrs->snake_names_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igCheckbox("##snake scores", &usrs->snake_scores);
      igCheckbox("##chat timestamps", &usrs->ntl_chat_timestamps);
      igCheckbox("##stealth mode", &usrs->ntl_stealth_mode);
      igCheckbox("##show all tags", &usrs->show_tags);
      igBeginDisabled(!usrs->show_tags);
      igCheckbox("##show NTL tags", &usrs->show_ntl_tags);
      igCheckbox("##show Vlither tags", &usrs->show_vlither_tags);
      igEndDisabled();
      igCheckbox("##show own nickname ingame", &usrs->show_own_nickname_ingame);
      if (igIsItemHovered(0))
        igSetTooltip("Shows your own nickname above your snake in-game, the same way other players' names are shown. Requires the Show Names hotkey to be active.");
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##background style", &usrs->background_style,
                      (const char*[]){
                          "Classic", "Dark tiles", "USA Star", "Stained Glass",
                          "Snakey", "Seigaiha", "Rizz", "Red Cube",
                          "Purple Cube", "Paint", "Leaves", "Kitties",
                          "Hex Ice", "Hex B", "Blue Cube", "Asanoha",
                          "2016 Background", "Custom (Upload)"},
                      18, -1);
      igCheckbox("##smooth zoom", &usrs->smooth_zoom);
      igSetNextItemWidth(-1);
      igSliderFloat("##zoom step", &usrs->zoom_step, 0.05f, 0.5f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
      igSetNextItemWidth(-1);
      igColorEdit3("##border color", usrs->bd_color, ImGuiColorEditFlags_None);
      igSetNextItemWidth(-1);
      igSliderInt("##minimap size", &usrs->minimap_size, 96, 512, "%d px",
                  ImGuiSliderFlags_AlwaysClamp);
      igCheckbox("##clock map", &usrs->minimap_clock);
      igCheckbox("##dynamic minimap", &usrs->ntl_dynamic_minimap);
      igCheckbox("##border indicator", &usrs->ntl_border_indicator);
      igCheckbox("##show own minimap name", &usrs->minimap_show_own_name);
      igSetNextItemWidth(-1);
      igInputTextWithHint("##own minimap name", "Use nickname",
                          usrs->minimap_display_name,
                          sizeof usrs->minimap_display_name,
                          ImGuiInputTextFlags_None, NULL, NULL);
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
      igCheckbox("##instant death", &usrs->instant_death);
      if (igIsItemHovered(0))
        igSetTooltip("Skips the after-death spectator view and returns to the homepage right away. Instant restart above takes priority if both are on.");
      igCheckbox("##restart rc", &usrs->restart_rc);
      igCheckbox("##quit mc", &usrs->quit_mc);
#ifdef ANDROID
      igCheckbox("##record gameplay", &usrs->record_gameplay);
      if (igIsItemHovered(0))
        igSetTooltip("Starts after joining a real server and stops on the homepage. Android asks for screen-capture permission for each session.");
      igCheckbox("##screenshot on kill", &usrs->screenshot_on_kill);
      if (igIsItemHovered(0))
        igSetTooltip("Saves a local PNG about 150 ms after the server confirms your kill.");
#endif
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
#ifdef ANDROID
    igSpacing();
    if (igButton("Upload background from gallery", (ImVec2){-1, 0}))
      if (android_jni_request_custom_background()) {
        usrs->background_style = 17;
        save_user_settings(usrs);
      }
#endif
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
        igText("Graphics preset");
        igAlignTextToFramePadding();
        igText("Hide enemy tags");
        igAlignTextToFramePadding();
        igText("Hide enemy cosmetics");
        igAlignTextToFramePadding();
        igText("High-visibility skins");
        igAlignTextToFramePadding();
        igText("Snake Nicks+");
        igAlignTextToFramePadding();
        igText("Names above body");
        igAlignTextToFramePadding();
        igText("Skinless Peek (hold W)");
        igAlignTextToFramePadding();
        igText("Own true skin");
        igAlignTextToFramePadding();
        igText("Team true skins");
        igAlignTextToFramePadding();
        igText("Show background");
        igAlignTextToFramePadding();
        igText("Show accessories");
        igAlignTextToFramePadding();
        igText("Snake shadow");
        igAlignTextToFramePadding();
        igText("Shadow strength");
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
        igText("Center line (other snakes)");
        igAlignTextToFramePadding();
        igText("Head dot");
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
        igText("Food glow");
        igAlignTextToFramePadding();
        igText("Uniform food color");

        igTableSetColumnIndex(1);
        igSetNextItemWidth(-1);
        int selected_preset = usrs->mode_graphics_preset[i];
        if (igCombo_Str_arr("##graphics preset", &selected_preset,
                            (const char*[]){"Custom", "Competitive",
                                            "Low quality", "Normal",
                                            "High quality"}, 5, -1)) {
          if (selected_preset > 0)
            apply_mode_graphics_preset(usrs, i, selected_preset);
          else
            usrs->mode_graphics_preset[i] = 0;
        }
        igCheckbox("##hide enemy tags", &usrs->mode_hide_enemy_tags[i]);
        igCheckbox("##hide enemy cosmetics",
                   &usrs->mode_hide_enemy_cosmetics[i]);
        igCheckbox("##high visibility skins",
                   &usrs->mode_high_visibility_skins[i]);
        igCheckbox("##nicks plus", &usrs->mode_nicks_plus[i]);
        igCheckbox("##names above", &usrs->mode_names_on_top[i]);
        igCheckbox("##skinless peek", &usrs->mode_skinless_peek[i]);
        igCheckbox("##own true skin", &usrs->mode_own_true_skin[i]);
        igCheckbox("##team true skins", &usrs->mode_team_true_skin[i]);
        igCheckbox("##bg", &mode->show_background);
        igCheckbox("##acc", &mode->show_accessories);
        igCheckbox("##shad", &mode->show_shadows);
        igBeginDisabled(!mode->show_shadows);
        igSetNextItemWidth(-1);
        igSliderFloat("##shadow strength", &usrs->snake_shadow_strength[i],
                      0.0f, 3.0f, "%.2fx",
                      ImGuiSliderFlags_AlwaysClamp);
        igEndDisabled();
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
        igCheckbox("##center line others", &usrs->center_line_others[i]);
        /* Keep the existing persisted show_crosshair field for settings-file
           compatibility, but use it as a local-player head-dot toggle. */
        igCheckbox("##head dot", &mode->show_crosshair);

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
                        (const char*[]){"Solid", "Rings", "Square food"}, 3, -1);
        igSetNextItemWidth(-1);
        igSliderFloat("##food scale", &mode->food_scale, 0.25f, 3, "%.2f",
                      ImGuiSliderFlags_AlwaysClamp);
        igCheckbox("##food float", &mode->food_float);
        igCheckbox("##food flicker", &mode->food_flicker);
        igCheckbox("##food glow", &usrs->food_glow[i]);
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
      igText("Cycle snake shader");
      igText("Toggle invisible own skin");
      igTextDisabled("SOS help (fixed S)");
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
      igSetNextItemWidth(frame_height * 2.2f);
      draw_vlither_key_selector("##shader cycle key", &usrs->shader_cycle_key);
      igSetNextItemWidth(frame_height * 2.2f);
      draw_vlither_key_selector("##invisible skin key",
                                &usrs->invisible_skin_key);
      igTextDisabled("S = 4 min + Help me!");
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

  /* Responsive two-column footer. Backup actions share one row immediately
     above the final Reset / OK row, keeping the primary confirmation in the
     bottom-right on every Android screen width. */
  float footer_x = style->WindowPadding.x;
  float footer_w = settings_window_size.x - style->WindowPadding.x * 2.0f;
  float btn_w = (footer_w - style->ItemSpacing.x) * 0.5f;
  float btn_h = footer_btn_h;
  float col1_x = footer_x;
  float col2_x = footer_x + btn_w + style->ItemSpacing.x;
  float bottom_y = settings_window_size.y - style->WindowPadding.y - btn_h;
  float backup_y = bottom_y - style->ItemSpacing.y - btn_h;
#ifdef ANDROID
  igSetCursorPosX(col1_x);
  igSetCursorPosY(backup_y);
  if (igButton("Create backup", (ImVec2){btn_w, btn_h})) {
    /* Export the current in-memory values, including Controls and custom
       buttons, rather than the last values written by the OK button. */
    save_user_settings(usrs);
    if (!android_jni_request_settings_backup())
      settings_transfer_unavailable = true;
  }
  if (igIsItemHovered(0))
    igSetTooltip("Saves settings, Controls, custom buttons and the uploaded arrow. Keep the backup private because it also contains saved team IDs and keys.");
  igSetCursorPosX(col2_x);
  igSetCursorPosY(backup_y);
  if (igButton("Load backup", (ImVec2){btn_w, btn_h}))
    igOpenPopup_Str("Load Vlither backup?", 0);
#endif
  igSetCursorPosX(col1_x);
  igSetCursorPosY(bottom_y);
  if (igButton("Reset", (ImVec2){btn_w, btn_h})) {
    user_settings_default(usrs);
    env->config.vsync = usrs->vsync;
    twindow_request_refresh(env->wnd);
  }
  igSetCursorPosX(col2_x);
  igSetCursorPosY(bottom_y);
  if (igButton("OK", (ImVec2){btn_w, btn_h})) {
    save_user_settings(usrs);
    gdata->curr_screen = TITLE_SCREEN;
  }

#ifdef ANDROID
  float dialog_btn_w = fminf(btn_w, 220.0f);
  if (igBeginPopupModal("Load Vlither backup?", NULL,
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoSavedSettings)) {
    igTextWrapped("This replaces all current settings, Controls and custom buttons. Vlither will close after a successful load; reopen it to apply the backup.");
    igSpacing();
    igTextColored((ImVec4){0.96f, 0.73f, 0.25f, 1.0f},
                  "Only choose a backup you created in Vlither.");
    igSpacing();
    if (igButton("Cancel", (ImVec2){dialog_btn_w * 0.72f, btn_h}))
      igCloseCurrentPopup();
    igSameLine(0, style->ItemSpacing.x);
    if (igButton("Choose backup", (ImVec2){dialog_btn_w, btn_h})) {
      if (!android_jni_request_settings_restore())
        settings_transfer_unavailable = true;
      igCloseCurrentPopup();
    }
    igEndPopup();
  }
  if (settings_transfer_unavailable) {
    igOpenPopup_Str("Backup unavailable", 0);
    settings_transfer_unavailable = false;
  }
  if (igBeginPopupModal("Backup unavailable", NULL,
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoSavedSettings)) {
    igTextWrapped("Android could not open the file picker. Please try again.");
    if (igButton("OK##backup unavailable", (ImVec2){dialog_btn_w, btn_h}))
      igCloseCurrentPopup();
    igEndPopup();
  }
#endif

  igPopFont();
}

void ui_settings_destroy(tenv* env) {}
