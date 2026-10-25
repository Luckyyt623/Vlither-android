#include <ctype.h>
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
    if (preset == 1) {
      m->show_background = false;
      m->show_accessories = false;
      m->show_shadows = true;
      m->death_effect = false;
      m->food_flicker = false;
      m->food_float = false;
      m->food_type = 1;
      m->render_mode = 1;
      us->food_glow[mode_index] = false;
      us->snake_shadow_strength[mode_index] = 0.6f;
    } else if (preset == 2) {
      m->show_background = false;
      m->show_accessories = false;
      m->show_shadows = true;
      m->death_effect = false;
      m->food_flicker = false;
      m->food_float = false;
      m->food_type = 0;
      m->render_mode = 2;
      us->food_glow[mode_index] = false;
      us->snake_shadow_strength[mode_index] = 0.6f;
    } else {
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


/* ---- Settings search bar -------------------------------------------------
 * Labels and widgets live in two parallel columns, so each label registers
 * whether it matches the query (sf_vis[]) and each widget group replays that
 * decision in the same order. Every typed word must match label or keywords. */
static const char* sf_query = "";
static bool sf_vis[160];
static int  sf_n, sf_r;

static bool sf_has(const char* hay, const char* needle, size_t nlen) {
  if (!nlen) return true;
  for (; *hay; ++hay) {
    size_t i = 0;
    while (i < nlen && hay[i] &&
           tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
      ++i;
    if (i == nlen) return true;
  }
  return false;
}

static bool sf_match(const char* label, const char* kw) {
  const char* p = sf_query;
  while (*p) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char* e = p;
    while (*e && *e != ' ') ++e;
    size_t n = (size_t)(e - p);
    if (!sf_has(label, p, n) && !(kw && sf_has(kw, p, n))) return false;
    p = e;
  }
  return true;
}

static void sf_push(bool m) {
  if (sf_n < (int)(sizeof sf_vis / sizeof sf_vis[0])) sf_vis[sf_n] = m;
  ++sf_n;
}
static bool sf_next(void) {
  int i = sf_r++;
  return i < (int)(sizeof sf_vis / sizeof sf_vis[0]) ? sf_vis[i] : true;
}

#define SLBLK(txt, kw)                                   \
  do {                                                   \
    bool _m = sf_match((txt), (kw));                     \
    sf_push(_m);                                         \
    if (_m) {                                            \
      igAlignTextToFramePadding();                       \
      igText("%s", (txt));                               \
    }                                                    \
  } while (0)
#define SLBL(txt) SLBLK(txt, NULL)

static bool sf_vis_el_cb(const char* id, uint8_t* v) {
  bool b = *v != 0;
  bool ch = igCheckbox(id, &b);
  if (ch) *v = b ? 1 : 0;
  return ch;
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

  child_window_height =
      fmaxf(1.0f, child_window_height - (frame_height + style->ItemSpacing.y));
  static char s_set_search[48];
  igSetNextItemWidth(-1);
  igInputTextWithHint("##settings_search", "Search settings...", s_set_search,
                      sizeof s_set_search, ImGuiInputTextFlags_None, NULL,
                      NULL);
  sf_query = s_set_search;

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
      sf_n = 0;
      igIndent(style->WindowPadding.x);
      SLBL("VSync");
      SLBL("FPS limit");
      SLBL("Uncapped FPS");
      SLBL("Performance mode");
      SLBL("UI font size");
      SLBL("Stats font size");
      SLBL("Leaderboard font size");
      SLBL("Leaderboard title");
      SLBL("Title text colour");
      SLBL("Leaderboard style");
      SLBL("Player text colour");
      SLBL("Names font size");
      SLBL("Show snake scores");
      SLBL("Chat timestamps");
      SLBL("Stealth mode");
      SLBL("Show tags");
      SLBL("Show NTL tags");
      SLBL("Show Vlither tags");
      SLBL("Show own nickname");
      SLBL("Background style");
      SLBL("Smooth zoom");
      SLBL("Zoom step");
      SLBL("Border color");
      SLBL("Minimap size");
      SLBL("Clock map");
      SLBL("Dynamic minimap");
      SLBL("Border-distance indicator");
      SLBL("Show my minimap name");
      SLBL("My minimap name");
      SLBL("Custom minimap position");
      SLBL("Drag/resize minimap");
      SLBL("Minimap X");
      SLBL("Minimap Y");
      SLBL("Reset minimap position");
      SLBL("Instant restart");
      SLBL("Instant death");
      SLBL("Restart with right click");
      SLBL("Quit with middle click");
#ifdef ANDROID
      SLBL("Record gameplay in server");
      SLBL("Screenshot after kill");
#endif
      SLBL("Laser color");
      SLBL("Laser thickness");
#ifdef ANDROID
      SLBL("Joystick laser length");
#endif
      SLBL("Bot circle after score");
      SLBL("Bot radius multiplier");
      SLBLK("Visuals without bot", "manual shadow gameplay own snake no bot decision visuals master");
      SLBLK("Visual: prey fill", "blue fill prey target edible decision visuals");
      SLBLK("Visual: danger lines", "danger threat lines glow heat red orange yellow green warning decision visuals");
      SLBLK("Visual: collision rings", "collision collider rings red yellow circle crash enemy decision visuals");
      SLBLK("Visual: head circle", "head circle pulse yellow turn decision visuals");
      SLBLK("Visual: encircle zone", "encircle coil zone radius ring blue decision visuals");
      SLBLK("Visual: avoid arrows", "avoid avoidance arrow vector escape dodge decision visuals");
      SLBLK("Visual: food target", "food target green cross pellet decision visuals");
      SLBLK("Visual: goal marker", "goal destination magenta pink cross aim decision visuals");
      SLBLK("Visual: status badge", "status badge label text corner stage decision visuals");

      igTableSetColumnIndex(1);
      sf_r = 0;
      if (sf_next()) {
      if (igCheckbox("##vsync", &usrs->vsync)) {
        env->config.vsync = usrs->vsync;
        twindow_request_refresh(env->wnd);
      }
      }
      if (sf_next()) {
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
      }
      if (sf_next()) {
      bool uncapped_fps = !usrs->vsync && usrs->fps_limit == 0;
      if (igCheckbox("##uncapped fps", &uncapped_fps)) {
        if (uncapped_fps) {
          usrs->fps_limit = 0;
          usrs->vsync = false;
        } else {
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
      }
      if (sf_next()) {
      igCheckbox("##performance mode", &usrs->performance_mode);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##ui font size", (int*)&usrs->ui_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##stats font size", (int*)&usrs->stats_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##leaderboard font size", (int*)&usrs->lb_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igInputTextWithHint("##leaderboard title", "Vlither Leaderboard",
                          usrs->leaderboard_title,
                          sizeof usrs->leaderboard_title,
                          ImGuiInputTextFlags_None, NULL, NULL);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igColorEdit4("##leaderboard title colour",
                   usrs->leaderboard_title_color,
                   ImGuiColorEditFlags_AlphaBar);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##leaderboard style", &usrs->ntl_leaderboard_style,
                      (const char*[]){"Snake colours", "Top-10 gradient",
                                      "Single colour"}, 3, -1);
      }
      if (sf_next()) {
      igBeginDisabled(usrs->ntl_leaderboard_style != 2);
      igSetNextItemWidth(-1);
      igColorEdit4("##leaderboard player colour", usrs->ntl_leaderboard_color,
                   ImGuiColorEditFlags_AlphaBar);
      igEndDisabled();
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##snake name font size",
                      (int*)&usrs->snake_names_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      }
      if (sf_next()) {
      igCheckbox("##snake scores", &usrs->snake_scores);
      }
      if (sf_next()) {
      igCheckbox("##chat timestamps", &usrs->ntl_chat_timestamps);
      }
      if (sf_next()) {
      igCheckbox("##stealth mode", &usrs->ntl_stealth_mode);
      }
      if (sf_next()) {
      igCheckbox("##show all tags", &usrs->show_tags);
      }
      if (sf_next()) {
      igBeginDisabled(!usrs->show_tags);
      igCheckbox("##show NTL tags", &usrs->show_ntl_tags);
      igEndDisabled();
      }
      if (sf_next()) {
      igBeginDisabled(!usrs->show_tags);
      igCheckbox("##show Vlither tags", &usrs->show_vlither_tags);
      igEndDisabled();
      }
      if (sf_next()) {
      igCheckbox("##show own nickname ingame", &usrs->show_own_nickname_ingame);
      if (igIsItemHovered(0))
        igSetTooltip("Shows your own nickname above your snake in-game, the same way other players' names are shown. Requires the Show Names hotkey to be active.");
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##background style", &usrs->background_style,
                      (const char*[]){
                          "Classic", "Dark tiles", "USA Star", "Stained Glass",
                          "Snakey", "Seigaiha", "Rizz", "Red Cube",
                          "Purple Cube", "Paint", "Leaves", "Kitties",
                          "Hex Ice", "Hex B", "Blue Cube", "Asanoha",
                          "2016 Background", "Custom (Upload)"},
                      18, -1);
      }
      if (sf_next()) {
      igCheckbox("##smooth zoom", &usrs->smooth_zoom);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igSliderFloat("##zoom step", &usrs->zoom_step, 0.05f, 0.5f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igColorEdit3("##border color", usrs->bd_color, ImGuiColorEditFlags_None);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igSliderInt("##minimap size", &usrs->minimap_size, 96, 512, "%d px",
                  ImGuiSliderFlags_AlwaysClamp);
      }
      if (sf_next()) {
      igCheckbox("##clock map", &usrs->minimap_clock);
      }
      if (sf_next()) {
      igCheckbox("##dynamic minimap", &usrs->ntl_dynamic_minimap);
      }
      if (sf_next()) {
      igCheckbox("##border indicator", &usrs->ntl_border_indicator);
      }
      if (sf_next()) {
      igCheckbox("##show own minimap name", &usrs->minimap_show_own_name);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igInputTextWithHint("##own minimap name", "Use nickname",
                          usrs->minimap_display_name,
                          sizeof usrs->minimap_display_name,
                          ImGuiInputTextFlags_None, NULL, NULL);
      }
      if (sf_next()) {
      igCheckbox("##minimap custom", &usrs->minimap_pos_custom);
      }
      if (sf_next()) {
      igCheckbox("##minimap drag", &usrs->minimap_drag_enabled);
      }
      if (sf_next()) {
      igBeginDisabled(!usrs->minimap_pos_custom);
      igSetNextItemWidth(-1);
      igSliderFloat("##minimap x", &usrs->minimap_rel_x, 0.0f, 1.0f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
      igEndDisabled();
      }
      if (sf_next()) {
      igBeginDisabled(!usrs->minimap_pos_custom);
      igSetNextItemWidth(-1);
      igSliderFloat("##minimap y", &usrs->minimap_rel_y, 0.0f, 1.0f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
      igEndDisabled();
      }
      if (sf_next()) {
      if (igButton("Reset##minimap", (ImVec2){-1, 0})) {
        usrs->minimap_pos_custom = false;
        usrs->minimap_rel_x = 0.84f;
        usrs->minimap_rel_y = 0.78f;
      }
      }
      if (sf_next()) {
      igCheckbox("##instant restart", &usrs->instant_restart);
      }
      if (sf_next()) {
      igCheckbox("##instant death", &usrs->instant_death);
      if (igIsItemHovered(0))
        igSetTooltip("Skips the after-death spectator view and returns to the homepage right away. Instant restart above takes priority if both are on.");
      }
      if (sf_next()) {
      igCheckbox("##restart rc", &usrs->restart_rc);
      }
      if (sf_next()) {
      igCheckbox("##quit mc", &usrs->quit_mc);
      }
#ifdef ANDROID
      if (sf_next()) {
      igCheckbox("##record gameplay", &usrs->record_gameplay);
      if (igIsItemHovered(0))
        igSetTooltip("Starts after joining a real server and stops on the homepage. Android asks for screen-capture permission for each session.");
      }
      if (sf_next()) {
      igCheckbox("##screenshot on kill", &usrs->screenshot_on_kill);
      if (igIsItemHovered(0))
        igSetTooltip("Saves a local PNG about 150 ms after the server confirms your kill.");
      }
#endif
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igColorEdit4("##laser color", usrs->laser_color,
                   ImGuiColorEditFlags_AlphaBar);
      }
      if (sf_next()) {
      igSetNextItemWidth(-1);
      igSliderInt("##laser thickness", &usrs->laser_thickness, 1, 4, "%d px",
                  ImGuiSliderFlags_AlwaysClamp);
      }
#ifdef ANDROID
      if (sf_next()) {
      {
        int laser_len = user_laser_len_pct(usrs);
        igSetNextItemWidth(-1);
        if (igSliderInt("##laser length", &laser_len, 10, 200, "%d%%",
                        ImGuiSliderFlags_AlwaysClamp))
          user_set_laser_len_pct(usrs, laser_len);
        if (igIsItemHovered(0))
          igSetTooltip("How far the Assist laser reaches when using the joystick, as a % of the screen's shorter side. It still stops at the screen edge. Default 95%.");
      }
      }
#endif
      if (sf_next()) {
                  igSetNextItemWidth(-1);
      igSliderInt("##circle after", &usrs->bot_follow_circle_score, 1000, 6000, "%d",
                  ImGuiSliderFlags_AlwaysClamp);
      }
      if (sf_next()) {
                  igSetNextItemWidth(-1);
      igSliderInt("##rad mult", &usrs->bot_radius_mult, 10, 40, "%dx",
                  ImGuiSliderFlags_AlwaysClamp);
      }
      if (sf_next()) {
      igCheckbox("##vis_manual", &usrs->vis_manual);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_0", &usrs->vis_el[0]);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_1", &usrs->vis_el[1]);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_2", &usrs->vis_el[2]);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_3", &usrs->vis_el[3]);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_4", &usrs->vis_el[4]);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_5", &usrs->vis_el[5]);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_6", &usrs->vis_el[6]);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_7", &usrs->vis_el[7]);
      }
      if (sf_next()) {
      sf_vis_el_cb("##vis_el_8", &usrs->vis_el[8]);
      }
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
        sf_n = 0;
      igIndent(style->WindowPadding.x);
        SLBL("Graphics preset");
        SLBL("Hide enemy tags");
        SLBL("Hide enemy cosmetics");
        SLBL("High-visibility skins");
        SLBL("Snake Nicks+");
        SLBL("Names above body");
        SLBL("Skinless Peek (hold W)");
        SLBL("Own true skin");
        SLBL("Team true skins");
        SLBL("Show background");
        SLBL("Show accessories");
        SLBL("Snake shadow");
        SLBL("Shadow strength");
        SLBL("Death effect");
        SLBL("Outline player names");
        SLBL("Segment separation");
        SLBL("Background scale");
        SLBL("Render mode");
        SLBL("Transparent skin");
        SLBL("Skin opacity");
        SLBL("Skinless opacity");
        SLBL("Center line (your snake)");
        SLBL("Center line (other snakes)");
        SLBL("Head dot");
        SLBL("Boost effect");
        SLBL("Boost effect strength");
        SLBL("Food shader");
        SLBL("Food scale (all)");
        SLBL("Normal food scale");
        SLBL("Death food scale");
        SLBL("Boost food scale");
        SLBL("Food float");
        SLBL("Food flicker");
        SLBL("Food glow");
        SLBL("Uniform food color");

        igTableSetColumnIndex(1);
      sf_r = 0;
      if (sf_next()) {
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
      }
      if (sf_next()) {
        igCheckbox("##hide enemy tags", &usrs->mode_hide_enemy_tags[i]);
      }
      if (sf_next()) {
        igCheckbox("##hide enemy cosmetics",
                   &usrs->mode_hide_enemy_cosmetics[i]);
      }
      if (sf_next()) {
        igCheckbox("##high visibility skins",
                   &usrs->mode_high_visibility_skins[i]);
      }
      if (sf_next()) {
        igCheckbox("##nicks plus", &usrs->mode_nicks_plus[i]);
      }
      if (sf_next()) {
        igCheckbox("##names above", &usrs->mode_names_on_top[i]);
      }
      if (sf_next()) {
        igCheckbox("##skinless peek", &usrs->mode_skinless_peek[i]);
      }
      if (sf_next()) {
        igCheckbox("##own true skin", &usrs->mode_own_true_skin[i]);
      }
      if (sf_next()) {
        igCheckbox("##team true skins", &usrs->mode_team_true_skin[i]);
      }
      if (sf_next()) {
        igCheckbox("##bg", &mode->show_background);
      }
      if (sf_next()) {
        igCheckbox("##acc", &mode->show_accessories);
      }
      if (sf_next()) {
        igCheckbox("##shad", &mode->show_shadows);
      }
      if (sf_next()) {
        igBeginDisabled(!mode->show_shadows);
        igSetNextItemWidth(-1);
        igSliderFloat("##shadow strength", &usrs->snake_shadow_strength[i],
                      0.0f, 3.0f, "%.2fx",
                      ImGuiSliderFlags_AlwaysClamp);
        igEndDisabled();
      }
      if (sf_next()) {
        igCheckbox("##death effect", &mode->death_effect);
      }
      if (sf_next()) {
        igCheckbox("##player names outline", &mode->player_names_outline);
      }
      if (sf_next()) {
        igSetNextItemWidth(-1);
        igSliderFloat("##bps", &mode->qsm, 1, 4, "%.2f",
                      ImGuiSliderFlags_AlwaysClamp);
      }
      if (sf_next()) {
        igSetNextItemWidth(-1);
        igSliderFloat("##bgs", &mode->bg_scale, 0.05, 4, "%.2fx",
                      ImGuiSliderFlags_AlwaysClamp);
      }
      if (sf_next()) {
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##render mode", &mode->render_mode,
                        (const char*[]){"Texture", "Orb", "Flat", "Skinless"}, 4, -1);
      }

      if (sf_next()) {
        igCheckbox("##transparent skin", &mode->transparent_skin);
      }
      if (sf_next()) {
        igBeginDisabled(!mode->transparent_skin);
        int opacity_percent =
            (int)(usrs->transparent_skin_opacity[i] * 100.0f + 0.5f);
        igSetNextItemWidth(-1);
        if (igSliderInt("##skin opacity", &opacity_percent, 15, 85, "%d%%",
                        ImGuiSliderFlags_AlwaysClamp))
          usrs->transparent_skin_opacity[i] = opacity_percent / 100.0f;
        igEndDisabled();
      }
      if (sf_next()) {
        igBeginDisabled(mode->render_mode != 3);
        int skinless_pct = user_skinless_opacity_pct(usrs, i);
        igSetNextItemWidth(-1);
        if (igSliderInt("##skinless opacity", &skinless_pct, 5, 100, "%d%%",
                        ImGuiSliderFlags_AlwaysClamp))
          user_set_skinless_opacity_pct(usrs, i, skinless_pct);
        if (igIsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          igSetTooltip("How see-through the Skinless body is. Needs render mode = Skinless. The center line stays 100% visible.");
        igEndDisabled();
      }
      if (sf_next()) {
        igCheckbox("##center line", &mode->center_line);
      }
      if (sf_next()) {
        igCheckbox("##center line others", &usrs->center_line_others[i]);
      }
      if (sf_next()) {
        igCheckbox("##head dot", &mode->show_crosshair);
      }

      if (sf_next()) {
        igCheckbox("##boost", &mode->show_boost);
        igSameLine(0, -1);
      }
      if (sf_next()) {
        igBeginDisabled(!mode->show_boost);
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##boost type", &mode->boost_type,
                        (const char*[]){"Normal", "Simple"}, 2, -1);
        igEndDisabled();
        igBeginDisabled(!mode->show_boost);
        igSetNextItemWidth(-1);
        igSliderFloat("##boost strength", &mode->boost_strength, 0.25f, 3,
                      "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        igEndDisabled();
      }
      if (sf_next()) {
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##food type", &mode->food_type,
                        (const char*[]){"Solid", "Rings", "Square food"}, 3, -1);
      }
      if (sf_next()) {
        igSetNextItemWidth(-1);
        igSliderFloat("##food scale", &mode->food_scale, 0.25f, 3, "%.2f",
                      ImGuiSliderFlags_AlwaysClamp);
        if (igIsItemHovered(0))
          igSetTooltip("Master size for every food type. Stacks with the three scales below (e.g. 2x here + 2x on death food = 4x actual size).");
      }
      if (sf_next()) {
        igSetNextItemWidth(-1);
        igSliderFloat("##normal food scale", &usrs->normal_food_scale[i],
                      0.25f, 3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (igIsItemHovered(0))
          igSetTooltip("Size of ordinary ambient food scattered across the map.");
      }
      if (sf_next()) {
        igSetNextItemWidth(-1);
        igSliderFloat("##death food scale", &usrs->death_food_scale[i], 0.25f,
                      3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (igIsItemHovered(0))
          igSetTooltip("Size of the food scattered when a snake dies.");
      }
      if (sf_next()) {
        igSetNextItemWidth(-1);
        igSliderFloat("##boost food scale", &usrs->boost_food_scale[i], 0.25f,
                      3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (igIsItemHovered(0))
          igSetTooltip("Size of the food dropped behind a boosting snake.");
      }
      if (sf_next()) {
        igCheckbox("##food float", &mode->food_float);
      }
      if (sf_next()) {
        igCheckbox("##food flicker", &mode->food_flicker);
      }
      if (sf_next()) {
        igCheckbox("##food glow", &usrs->food_glow[i]);
      }
      if (sf_next()) {
        igCheckbox("##uniform food color", &mode->uniform_food_color);
        igSameLine(0, -1);
        igBeginDisabled(!mode->uniform_food_color);
        igSetNextItemWidth(-1);
        igColorEdit3("##fdcolor", mode->food_color, ImGuiColorEditFlags_None);
        igEndDisabled();
      }
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
        if (!sf_match(hk->description, "hotkey key")) continue;
        igAlignTextToFramePadding();
        igText(hk->description);
      }
      const bool hk_shader = sf_match("Cycle snake shader", "hotkey key");
      const bool hk_invis = sf_match("Toggle invisible own skin", "hotkey key");
      const bool hk_sos = sf_match("SOS help (fixed S)", "hotkey key");
      if (hk_shader) igText("Cycle snake shader");
      if (hk_invis) igText("Toggle invisible own skin");
      if (hk_sos) igTextDisabled("SOS help (fixed S)");
      igTableSetColumnIndex(1);

      for (int i = 0; i < NUM_HOTKEYS; i++) {
        hotkey* hk = usrs->hotkeys + i;
        if (!sf_match(hk->description, "hotkey key")) continue;
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
      if (hk_shader) {
        igSetNextItemWidth(frame_height * 2.2f);
        draw_vlither_key_selector("##shader cycle key",
                                  &usrs->shader_cycle_key);
      }
      if (hk_invis) {
        igSetNextItemWidth(frame_height * 2.2f);
        draw_vlither_key_selector("##invisible skin key",
                                  &usrs->invisible_skin_key);
      }
      if (hk_sos) igTextDisabled("S = 4 min + Help me!");
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
