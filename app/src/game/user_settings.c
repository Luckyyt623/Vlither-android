#include "user_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <thermite.h>

#ifdef ANDROID
#include "../android_path.h"
#define OPEN_SETTINGS_FILE(mode) \
    (android_build_path(_settings_path, sizeof(_settings_path), USER_SETTINGS_FILE), \
     fopen(_settings_path, (mode)))
#else
#define OPEN_SETTINGS_FILE(mode) fopen(USER_SETTINGS_FILE, (mode))
#endif

bool g_ctrl_swap_sides = false;

void user_settings_default(user_settings* usr_settings) {
  usr_settings->ui_font_size = FONT_SIZE_SMALL;
  usr_settings->lb_font_size = FONT_SIZE_REGULAR;
  usr_settings->snake_names_font_size = FONT_SIZE_REGULAR;
  usr_settings->stats_font_size = FONT_SIZE_REGULAR;

  strcpy(usr_settings->version, SETTINGS_VERSION);

  usr_settings->bd_color[0] = 1;
  usr_settings->bd_color[1] = 0.25f;
  usr_settings->bd_color[2] = 0.25f;
  usr_settings->laser_color[0] = 0.5f;
  usr_settings->laser_color[1] = 1;
  usr_settings->laser_color[2] = 0.5f;
  usr_settings->laser_color[3] = 1;
  usr_settings->laser_thickness = 2;
  usr_settings->cursor_size = 48;
  usr_settings->minimap_size = 300;
  usr_settings->minimap_pos_custom = false;
  usr_settings->minimap_rel_x = 0.84f;
  usr_settings->minimap_rel_y = 0.78f;
  usr_settings->zoom_step = 0.1f;
  usr_settings->snake_scores = true;
  usr_settings->restart_rc = false;
  usr_settings->quit_mc = false;
  usr_settings->smooth_zoom = false;
  /* Stable display-paced rendering is the safe Android default.  Uncapped
     rendering remains available in Settings, but starting uncapped needlessly
     heats the phone and leads to thermal-throttling FPS spikes. */
  usr_settings->vsync = true;
  usr_settings->instant_restart = false;
  usr_settings->bot_radius_mult = 20;
  usr_settings->bot_follow_circle_score = 2000;
  usr_settings->ctrl_mode_trackpad = true;

  usr_settings->boost_pos_custom = false;
  usr_settings->boost_rel_x      = 0.875f;
  usr_settings->boost_rel_y      = 0.875f;
  usr_settings->boost_rel_size   = 0.125f;
  usr_settings->boost_opacity    = 1.0f;

  usr_settings->joy_pos_custom   = false;
  usr_settings->joy_rel_x        = 0.125f;
  usr_settings->joy_rel_y        = 0.825f;
  usr_settings->joy_rel_size     = 0.175f;
  usr_settings->joy_opacity      = 1.0f;

  usr_settings->zoom_sensitivity    = 1.0f;
  usr_settings->arrow_size          = 1.0f;
  usr_settings->arrow_sensitivity   = 1.0f;
  usr_settings->boost_arrow_anim    = false;
  usr_settings->arrow_invisible     = false;
  usr_settings->bot_vis             = true;
  usr_settings->zslider_rel_x       = 0.968f;
  usr_settings->zslider_rel_y       = 0.500f;
  usr_settings->zslider_rel_h       = 0.280f;
  usr_settings->zslider_opacity     = 1.0f;
  usr_settings->zslider_horizontal  = false;
  usr_settings->zslider_hidden      = false;

  for (int i = 0; i < NUM_HOTKEYS; i++)
    usr_settings->hk_show_btn[i] = false;
  usr_settings->ctrl_swap_sides    = false;

  usr_settings->ntl_enabled = true;
  usr_settings->ntl_team_id[0] = '\0';
  usr_settings->ntl_auth_key[0] = '\0';

  usr_settings->modes[0].food_flicker = true;
  usr_settings->modes[0].food_float = true;
  usr_settings->modes[0].uniform_food_color = false;
  usr_settings->modes[0].food_type = 0;
  usr_settings->modes[0].food_scale = 1;
  usr_settings->modes[0].food_color[0] = 1;
  usr_settings->modes[0].food_color[1] = 1;
  usr_settings->modes[0].food_color[2] = 1;
  usr_settings->modes[0].boost_type = 0;
  usr_settings->modes[0].qsm = 1;
  usr_settings->modes[0].bg_scale = 599 / 4096.0f;
  usr_settings->modes[0].boost_strength = 1;
  usr_settings->modes[0].show_crosshair = false;
  usr_settings->modes[0].show_boost = true;
  usr_settings->modes[0].show_shadows = true;
  usr_settings->modes[0].show_background = true;
  usr_settings->modes[0].show_accessories = true;
  usr_settings->modes[0].death_effect = true;
  usr_settings->modes[0].player_names_outline = false;
  usr_settings->modes[0].render_mode = 0;
  usr_settings->modes[0].transparent_skin = false;
  usr_settings->modes[0].center_line = false;

  usr_settings->modes[1].food_flicker = false;
  usr_settings->modes[1].food_float = false;
  usr_settings->modes[1].uniform_food_color = true;
  usr_settings->modes[1].food_type = 1;
  usr_settings->modes[1].food_scale = 1;
  usr_settings->modes[1].food_color[0] = 0.7f;
  usr_settings->modes[1].food_color[1] = 0.7f;
  usr_settings->modes[1].food_color[2] = 0.7f;
  usr_settings->modes[1].boost_type = 1;
  usr_settings->modes[1].qsm = 1;
  usr_settings->modes[1].bg_scale = 599 / 4096.0f;
  usr_settings->modes[1].boost_strength = 1;
  usr_settings->modes[1].show_crosshair = true;
  usr_settings->modes[1].show_boost = false;
  usr_settings->modes[1].show_shadows = true;
  usr_settings->modes[1].show_background = false;
  usr_settings->modes[1].show_accessories = false;
  usr_settings->modes[1].death_effect = false;
  usr_settings->modes[1].player_names_outline = true;
  usr_settings->modes[1].render_mode = 1;
  usr_settings->modes[1].transparent_skin = false;
  usr_settings->modes[1].center_line = false;

  usr_settings->hotkeys[HOTKEY_HUD] = (hotkey){GLFW_KEY_H, true, 0, "HUD"};
  usr_settings->hotkeys[HOTKEY_SHOW_NAMES] =
      (hotkey){GLFW_KEY_P, true, 0, "Show names"};
  usr_settings->hotkeys[HOTKEY_BIG_FOOD] =
      (hotkey){GLFW_KEY_F, false, 0, "Big food"};
  usr_settings->hotkeys[HOTKEY_ASSIST] =
      (hotkey){GLFW_KEY_K, false, 0, "Assist"};
  usr_settings->hotkeys[HOTKEY_BOT] =
      (hotkey){GLFW_KEY_T, false, 0, "Bot"};
  usr_settings->hotkeys[HOTKEY_MENU] =
      (hotkey){GLFW_KEY_Z, true, 0, "Hotkey menu"};
  usr_settings->hotkeys[HOTKEY_RESTART] =
      (hotkey){GLFW_KEY_R, false, 1, "Restart"};
  usr_settings->hotkeys[HOTKEY_QUIT] = (hotkey){GLFW_KEY_Q, false, 1, "Quit"};

  for (int i = 0; i < MAX_KEY_BTNS; i++) {
    usr_settings->key_btns[i].active   = false;
    usr_settings->key_btns[i].glfw_key = 0;
    usr_settings->key_btns[i].label[0] = '\0';
    usr_settings->key_btns[i].rel_x    = 0.5f;
    usr_settings->key_btns[i].rel_y    = 0.5f;
    usr_settings->key_btns[i].rel_size = 0.08f;
    usr_settings->key_btns[i].opacity  = 0.85f;
  }

  usr_settings->transparent_skin_opacity[0] = 0.35f;
  usr_settings->transparent_skin_opacity[1] = 0.35f;
  usr_settings->minimap_drag_enabled = false;
  usr_settings->fps_limit = 0;
  usr_settings->performance_mode = false;

  usr_settings->ntl_chat_rel_x = 0.012f;
  usr_settings->ntl_chat_rel_y = 0.020f;
  usr_settings->ntl_chat_rel_w = 0.36f;
  usr_settings->ntl_chat_rel_h = 0.44f;
  usr_settings->ntl_players_rel_x = 0.72f;
  usr_settings->ntl_players_rel_y = 0.020f;
  usr_settings->ntl_players_rel_w = 0.265f;
  usr_settings->ntl_players_rel_h = 0.44f;

  usr_settings->ntl_chat_minimized = false;
  usr_settings->ntl_show_teammates = true;
  usr_settings->ntl_marker_labels = true;
  usr_settings->ntl_marker_shape = 0;
  usr_settings->ntl_marker_size = 5.0f;
  usr_settings->ntl_marker_color[0] = 0.05f;
  usr_settings->ntl_marker_color[1] = 1.0f;
  usr_settings->ntl_marker_color[2] = 0.55f;
  usr_settings->ntl_marker_color[3] = 1.0f;
  usr_settings->own_marker_shape = 0;
  usr_settings->own_marker_size = 5.5f;
  usr_settings->own_marker_color[0] = 1.0f;
  usr_settings->own_marker_color[1] = 0.35f;
  usr_settings->own_marker_color[2] = 0.35f;
  usr_settings->own_marker_color[3] = 1.0f;
  usr_settings->ntl_team_profile_count = 0;
  usr_settings->ntl_active_team_profile = -1;
  memset(usr_settings->ntl_team_profiles, 0,
         sizeof usr_settings->ntl_team_profiles);
  usr_settings->ntl_client_id[0] = '\0';
  usr_settings->ntl_tag_id = -1;
  usr_settings->ntl_tag_password_md5[0] = '\0';
  usr_settings->vlither_tag_backend_url[0] = '\0';
  usr_settings->vlither_tag_id = -1;
  usr_settings->vlither_tag_name[0] = '\0';
  usr_settings->show_tags = true;
  usr_settings->show_ntl_tags = true;
  usr_settings->show_vlither_tags = true;
  usr_settings->background_style = 0;
  for (int i = 0; i < MAX_KEY_BTNS; ++i) usr_settings->key_btn_shape[i] = 0;
  usr_settings->tag_size_scale = 1.0f;
  usr_settings->tag_size_with_zoom = true;
  usr_settings->external_input_mode = 0;
  usr_settings->vlither_show_minimap_players = true;
  usr_settings->vlither_show_player_stats = true;
  usr_settings->ntl_show_player_stats = true;
  usr_settings->voice_chat_enabled = false;
  memset(usr_settings->voice_settings_reserved, 0,
         sizeof usr_settings->voice_settings_reserved);
  usr_settings->voice_status_icons_hidden = false;
  memset(usr_settings->arrow_head_settings_reserved, 0,
         sizeof usr_settings->arrow_head_settings_reserved);
  usr_settings->arrow_sync_with_zoom = true;
  usr_settings->head_dot_color[0] = 1.0f;
  usr_settings->head_dot_color[1] = 1.0f;
  usr_settings->head_dot_color[2] = 1.0f;
  memset(usr_settings->arrow_style_settings_reserved, 0,
         sizeof usr_settings->arrow_style_settings_reserved);
  usr_settings->arrow_style = 0;
  memset(usr_settings->homepage_settings_reserved, 0,
         sizeof usr_settings->homepage_settings_reserved);
  /* Normal Vlither is 0; Galaxy is the requested first-run default. */
  usr_settings->homepage_background = 5;
  usr_settings->homepage_blur = 0.0f;
  memset(usr_settings->vlither_feature_settings_reserved, 0,
         sizeof usr_settings->vlither_feature_settings_reserved);
  strcpy(usr_settings->leaderboard_title, "Vlither Leaderboard");
  usr_settings->food_glow[0] = false;
  usr_settings->food_glow[1] = false;
  usr_settings->center_line_others[0] = false;
  usr_settings->center_line_others[1] = false;
  usr_settings->snake_shadow_strength[0] = 1.0f;
  usr_settings->snake_shadow_strength[1] = 1.0f;
  usr_settings->minimap_display_name[0] = '\0';
  usr_settings->minimap_show_own_name = false;
  usr_settings->vlither_chat_joined = true;
  usr_settings->shader_cycle_key = GLFW_KEY_G;
  usr_settings->invisible_skin_key = GLFW_KEY_I;
  usr_settings->shader_cycle_index = -1;
  usr_settings->own_skin_invisible = false;
  usr_settings->zslider_thickness = 1.0f;
  usr_settings->zslider_thumb_scale = 1.0f;
  usr_settings->custom_arrow_enabled = false;
  usr_settings->server_address_filter = 0;
  strcpy(usr_settings->server_address, "148.113.20.151:444");
  memset(usr_settings->clock_map_settings_reserved, 0,
         sizeof usr_settings->clock_map_settings_reserved);
  usr_settings->minimap_clock = false;
  memset(usr_settings->ntl_competitive_settings_reserved, 0,
         sizeof usr_settings->ntl_competitive_settings_reserved);
  usr_settings->ntl_hide_enemy_tags = false;
  usr_settings->ntl_hide_enemy_cosmetics = false;
  usr_settings->ntl_high_visibility_skins = false;
  usr_settings->ntl_nicks_plus = true;
  usr_settings->ntl_names_on_top = true;
  usr_settings->ntl_legacy_option_1 = false;
  usr_settings->ntl_legacy_option_2 = false;
  usr_settings->ntl_legacy_option_3 = false;
  usr_settings->ntl_chat_timestamps = true;
  usr_settings->ntl_dynamic_minimap = true;
  usr_settings->ntl_border_indicator = true;
  usr_settings->ntl_skinless_peek = false;
  usr_settings->ntl_own_true_skin = true;
  usr_settings->ntl_team_true_skin = true;
  usr_settings->ntl_graphics_preset = 3;
  usr_settings->ntl_leaderboard_style = 1;
  usr_settings->ntl_leaderboard_color[0] = 1.0f;
  usr_settings->ntl_leaderboard_color[1] = 0.82f;
  usr_settings->ntl_leaderboard_color[2] = 0.22f;
  usr_settings->ntl_leaderboard_color[3] = 1.0f;
  usr_settings->ntl_stealth_mode = false;
  memset(usr_settings->integrated_mode_settings_reserved, 0,
         sizeof usr_settings->integrated_mode_settings_reserved);
  for (int i = 0; i < 2; ++i) {
    usr_settings->mode_hide_enemy_tags[i] = false;
    usr_settings->mode_hide_enemy_cosmetics[i] = false;
    usr_settings->mode_high_visibility_skins[i] = false;
    usr_settings->mode_nicks_plus[i] = true;
    usr_settings->mode_names_on_top[i] = true;
    usr_settings->mode_skinless_peek[i] = false;
    usr_settings->mode_own_true_skin[i] = true;
    usr_settings->mode_team_true_skin[i] = true;
    usr_settings->mode_graphics_preset[i] = 3;
  }
  memset(usr_settings->capture_settings_reserved, 0,
         sizeof usr_settings->capture_settings_reserved);
  usr_settings->record_gameplay = false;
  usr_settings->screenshot_on_kill = false;
  memset(usr_settings->ratings_settings_reserved, 0,
         sizeof usr_settings->ratings_settings_reserved);
  usr_settings->ratings_owner_token[0] = 0;
  memset(usr_settings->leaderboard_title_settings_reserved, 0,
         sizeof usr_settings->leaderboard_title_settings_reserved);
  usr_settings->leaderboard_title_color[0] = 1.0f;
  usr_settings->leaderboard_title_color[1] = 0.88f;
  usr_settings->leaderboard_title_color[2] = 0.30f;
  usr_settings->leaderboard_title_color[3] = 0.96f;
  memset(usr_settings->vlither_profile_settings_reserved, 0,
         sizeof usr_settings->vlither_profile_settings_reserved);
  usr_settings->vlither_profile_color_custom = false;
  usr_settings->vlither_profile_color[0] = 0.35f;
  usr_settings->vlither_profile_color[1] = 0.68f;
  usr_settings->vlither_profile_color[2] = 1.0f;
  usr_settings->vlither_profile_emoji = 0;
  memset(usr_settings->chat_hud_settings_reserved, 0,
         sizeof usr_settings->chat_hud_settings_reserved);
  usr_settings->vlither_chat_hud_visible = true;
  usr_settings->ntl_chat_hud_visible = true;
  memset(usr_settings->friends_panel_settings_reserved, 0,
         sizeof usr_settings->friends_panel_settings_reserved);
  usr_settings->friends_panel_zoom = 1.0f;
  memset(usr_settings->snakey_rain_settings_reserved, 0,
         sizeof usr_settings->snakey_rain_settings_reserved);
  usr_settings->snakey_rain_enabled = false;
  strcpy(usr_settings->snakey_rain_username, "snakeyuser");
  strcpy(usr_settings->snakey_rain_password, "wormfood");
  usr_settings->snakey_rain_max_bots = 1000;
  strcpy(usr_settings->snakey_rain_bot_name, "SnakeyRain");
  strcpy(usr_settings->snakey_rain_bot_skin,
         "uuuuuuuauuuuuuaauuuuuaaauuuuaaaauuuaaaaauuaaaaaauaaaaaaa"
         "uuaaaaaauuuaaaaauuuuaaaauuuuuaaauuuuuuaa");
}

void write_default_settings(user_settings* usr_settings) {
  user_settings_default(usr_settings);

#ifdef ANDROID
  char _settings_path[512];
#endif
  FILE* f = OPEN_SETTINGS_FILE("wb");
  if (f == NULL) {

#ifdef ANDROID
    return;
#else
    printf("Error creating settings file.");
    exit(-1);
#endif
  }

  fwrite(usr_settings, sizeof(user_settings), 1, f);
  fclose(f);
}

void read_user_settings(user_settings* usr_settings) {
#ifdef ANDROID
  char _settings_path[512];
#endif
  FILE* f = OPEN_SETTINGS_FILE("rb");

  if (f == NULL) {
    write_default_settings(usr_settings);
    return;
  }

  /* Seed a temporary value with current defaults, then overwrite only the
     bytes that exist in the file. v2.3 files are a strict prefix of v2.4, so
     newly appended options receive safe defaults while all old preferences
     survive the upgrade. */
  user_settings loaded;
  user_settings_default(&loaded);

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    write_default_settings(usr_settings);
    return;
  }
  long file_size = ftell(f);
  rewind(f);
  if (file_size <= 0) {
    fclose(f);
    write_default_settings(usr_settings);
    return;
  }

  /* Each appended settings generation reuses space that may have been tail
     padding in the previous layout. Distinguish the known logical prefixes so
     legacy padding never overwrites newer defaults or the persistent NTL ID. */
  size_t v23_prefix = offsetof(user_settings, transparent_skin_opacity);
  size_t v24_prefix = offsetof(user_settings, ntl_chat_minimized);
  size_t v25_prefix = offsetof(user_settings, ntl_client_id);
  size_t v254_prefix = offsetof(user_settings, ntl_tag_id);
  size_t v255_prefix = offsetof(user_settings, vlither_tag_backend_url);
  size_t v256_prefix = offsetof(user_settings, show_tags);
  size_t v26_prefix = offsetof(user_settings, background_style);
  size_t v27_prefix = offsetof(user_settings, tag_size_scale);
  size_t v28_prefix = offsetof(user_settings, external_input_mode);
  size_t v29_prefix = offsetof(user_settings, vlither_show_minimap_players);
  size_t v30_prefix = offsetof(user_settings, voice_chat_enabled);
  size_t v31_prefix = offsetof(user_settings, arrow_head_settings_reserved);
  size_t v32_prefix = offsetof(user_settings, arrow_style_settings_reserved);
  size_t v33_prefix = offsetof(user_settings, homepage_settings_reserved);
  size_t v34_prefix = offsetof(user_settings, vlither_feature_settings_reserved);
  size_t v35_prefix = offsetof(user_settings, clock_map_settings_reserved);
  size_t v36_prefix = offsetof(user_settings, ntl_competitive_settings_reserved);
  size_t v37_prefix = offsetof(user_settings, integrated_mode_settings_reserved);
  size_t v38_prefix = offsetof(user_settings, capture_settings_reserved);
  size_t v40_prefix = offsetof(user_settings, ratings_settings_reserved);
  size_t v41_prefix =
      offsetof(user_settings, leaderboard_title_settings_reserved);
  size_t v42_prefix =
      offsetof(user_settings, vlither_profile_settings_reserved);
  size_t v43_prefix =
      offsetof(user_settings, chat_hud_settings_reserved);
  size_t v44_prefix =
      offsetof(user_settings, friends_panel_settings_reserved);
  size_t v45_prefix =
      offsetof(user_settings, snakey_rain_settings_reserved);
  bool migrate_single_mode_features =
      (size_t)file_size >= v37_prefix && (size_t)file_size < v38_prefix;
  size_t bytes_to_read;
  if ((size_t)file_size >= sizeof loaded)
    bytes_to_read = sizeof loaded;
  else if ((size_t)file_size >= v45_prefix)
    /* Preserve every v4.4 value while keeping Snakey Rain disabled until the
       player explicitly opts in from the homepage. */
    bytes_to_read = v45_prefix;
  else if ((size_t)file_size >= v44_prefix)
    /* Preserve both v4.3 chat-HUD visibility values while initializing the
       new player-list zoom to 1.0x. */
    bytes_to_read = v44_prefix;
  else if ((size_t)file_size >= v43_prefix)
    /* Preserve the complete public-profile build while keeping both gameplay
       chat HUDs visible by default for existing users. */
    bytes_to_read = v43_prefix;
  else if ((size_t)file_size >= v42_prefix)
    /* Preserve the complete leaderboard-title build while initializing the
       new public profile to automatic colour and no emoji. */
    bytes_to_read = v42_prefix;
  else if ((size_t)file_size >= v41_prefix)
    /* Preserve the complete ratings build while giving the title its
       original gold colour by default. */
    bytes_to_read = v41_prefix;
  else if ((size_t)file_size >= v40_prefix)
    /* Preserve v3.9 capture preferences while generating a fresh anonymous
       review-ownership token on first use. */
    bytes_to_read = v40_prefix;
  else if ((size_t)file_size >= v38_prefix)
    /* Preserve every independent Normal/Assist preference from v3.8 while
       initializing local capture controls to disabled. */
    bytes_to_read = v38_prefix;
  else if ((size_t)file_size >= v37_prefix)
    /* Preserve the complete first feature-pack build while initializing the
       new independent Normal/Assist values from safe defaults. */
    bytes_to_read = v37_prefix;
  else if ((size_t)file_size >= v36_prefix)
    /* Preserve the full clock-map build while keeping all NTL competitive
       controls on their deterministic defaults. */
    bytes_to_read = v36_prefix;
  else if ((size_t)file_size >= v35_prefix)
    /* Preserve the complete crash-fixed v4.7.1 settings while leaving the
       new clock-map option disabled by default. */
    bytes_to_read = v35_prefix;
  else if ((size_t)file_size > v34_prefix) {
    /* The first v4.7 preview enlarged MAX_IPV4_LEN inside the persisted
       prefix. Loading that shifted layout corrupts later settings and can
       crash while opening the game, so replace it with safe defaults. */
    fclose(f);
    write_default_settings(usr_settings);
    return;
  }
  else if ((size_t)file_size >= v34_prefix)
    /* v3.3 contains the complete homepage selection. Ignore its compiler tail
       padding so every new v3.4 option receives a deterministic default. */
    bytes_to_read = v34_prefix;
  else if ((size_t)file_size >= v33_prefix)
    /* Preserve the selected v3.2 arrow while keeping Galaxy and zero blur as
       the new homepage defaults. */
    bytes_to_read = v33_prefix;
  else if ((size_t)file_size >= v32_prefix)
    /* v3.1 contains the complete Arrow sync and head-dot preferences. Ignore
       only its old tail padding so the new arrow style stays Red Arrow. */
    bytes_to_read = v32_prefix;
  else if ((size_t)file_size >= v31_prefix)
    /* v3.0 contains the complete Voice status-icon preference. Ignore only
       its old tail padding so Arrow sync and head-dot colour use defaults. */
    bytes_to_read = v31_prefix;
  else if ((size_t)file_size >= v30_prefix)
    /* v2.9 contains all telemetry controls. Ignore its old tail padding so
       the new voice-status visibility preference keeps its default. */
    bytes_to_read = v30_prefix;
  else if ((size_t)file_size >= v29_prefix)
    /* v2.8 includes the complete external mouse/keyboard field. Ignore only
       its final struct padding so the new telemetry defaults remain intact. */
    bytes_to_read = v29_prefix;
  else if ((size_t)file_size >= v28_prefix)
    /* v2.7 ended before the external mouse/keyboard mode. Its file may
       include compiler tail padding, so do not copy that padding over the
       new mode's default value. */
    bytes_to_read = v28_prefix;
  else if ((size_t)file_size >= v27_prefix)
    /* Preserve the complete v2.6 background and keyboard-button settings. */
    bytes_to_read = v27_prefix;
  else if ((size_t)file_size >= v26_prefix)
    /* Builds immediately before the background-style / button-shape fields
       contain the full visibility controls followed only by tail padding. */
    bytes_to_read = v26_prefix;
  else if ((size_t)file_size >= v256_prefix)
    /* Builds immediately before the visibility controls contain the complete
       Vlither tag settings followed only by compiler tail padding. */
    bytes_to_read = v256_prefix;
  else if ((size_t)file_size >= v255_prefix)
    /* The prior NTL-tag build ended immediately before the Vlither backend
       fields. Ignore its compiler tail padding. */
    bytes_to_read = v255_prefix;
  else if ((size_t)file_size >= v254_prefix)
    /* v2.5.4 ended before the appended NTL tag settings. Ignore any old tail
       padding so it cannot overwrite the new defaults. */
    bytes_to_read = v254_prefix;
  else if ((size_t)file_size >= v25_prefix)
    /* v2.5 ended at v25_prefix, followed only by compiler tail padding. Do not
       copy that padding into the new persistent NTL client ID. */
    bytes_to_read = v25_prefix;
  else if ((size_t)file_size >= v24_prefix)
    /* A complete v2.4 file is larger than v24_prefix only because the struct
       was rounded up to 16-byte alignment. Ignore those final padding bytes. */
    bytes_to_read = v24_prefix;
  else
    bytes_to_read = (size_t)file_size < v23_prefix
                        ? (size_t)file_size
                        : v23_prefix;
  size_t bytes_read = fread(&loaded, 1, bytes_to_read, f);
  fclose(f);

  if (bytes_read < sizeof loaded.version ||
      strncmp(loaded.version, SETTINGS_VERSION, strlen(SETTINGS_VERSION)) != 0) {
    printf("Settings file outdated, recreating with default settings.\n");
    write_default_settings(usr_settings);
    return;
  }

  /* Validate appended settings in case a truncated or hand-edited file was
     loaded. */
  for (int i = 0; i < 2; ++i) {
    if (loaded.transparent_skin_opacity[i] < 0.15f ||
        loaded.transparent_skin_opacity[i] > 0.85f)
      loaded.transparent_skin_opacity[i] = 0.35f;
  }
  if (loaded.fps_limit != 0 && loaded.fps_limit != 60 &&
      loaded.fps_limit != 90 && loaded.fps_limit != 120 &&
      loaded.fps_limit != 144 && loaded.fps_limit != 165 &&
      loaded.fps_limit != 240 && loaded.fps_limit != 360)
    loaded.fps_limit = 0;

  const float ntl_defaults[8] = {
      0.012f, 0.020f, 0.36f, 0.44f,
      0.72f, 0.020f, 0.265f, 0.44f};
  float* ntl_values = &loaded.ntl_chat_rel_x;
  for (int i = 0; i < 8; ++i) {
    bool is_size = (i == 2 || i == 3 || i == 6 || i == 7);
    float min_value = is_size ? 0.10f : -0.25f;
    float max_value = is_size ? 1.00f : 1.25f;
    if (!isfinite(ntl_values[i]) || ntl_values[i] < min_value ||
        ntl_values[i] > max_value)
      ntl_values[i] = ntl_defaults[i];
  }
  if (loaded.ntl_marker_shape < 0 || loaded.ntl_marker_shape > 2)
    loaded.ntl_marker_shape = 0;
  if (loaded.own_marker_shape < 0 || loaded.own_marker_shape > 2)
    loaded.own_marker_shape = 0;
  if (!isfinite(loaded.ntl_marker_size) || loaded.ntl_marker_size < 2.0f ||
      loaded.ntl_marker_size > 14.0f)
    loaded.ntl_marker_size = 5.0f;
  if (!isfinite(loaded.own_marker_size) || loaded.own_marker_size < 2.0f ||
      loaded.own_marker_size > 14.0f)
    loaded.own_marker_size = 5.5f;
  for (int c = 0; c < 4; ++c) {
    if (!isfinite(loaded.ntl_marker_color[c]) ||
        loaded.ntl_marker_color[c] < 0.0f || loaded.ntl_marker_color[c] > 1.0f)
      loaded.ntl_marker_color[c] =
          (const float[]){0.05f, 1.0f, 0.55f, 1.0f}[c];
    if (!isfinite(loaded.own_marker_color[c]) ||
        loaded.own_marker_color[c] < 0.0f || loaded.own_marker_color[c] > 1.0f)
      loaded.own_marker_color[c] =
          (const float[]){1.0f, 0.35f, 0.35f, 1.0f}[c];
  }
  if (loaded.ntl_team_profile_count < 0 ||
      loaded.ntl_team_profile_count > MAX_NTL_TEAM_PROFILES)
    loaded.ntl_team_profile_count = 0;
  if (loaded.ntl_active_team_profile < -1 ||
      loaded.ntl_active_team_profile >= loaded.ntl_team_profile_count)
    loaded.ntl_active_team_profile = -1;
  for (int i = 0; i < loaded.ntl_team_profile_count; ++i) {
    loaded.ntl_team_profiles[i].name[MAX_NTL_TEAM_NAME] = 0;
    loaded.ntl_team_profiles[i].team_id[95] = 0;
    loaded.ntl_team_profiles[i].auth_key[95] = 0;
    if (!loaded.ntl_team_profiles[i].name[0]) {
      if (loaded.ntl_active_team_profile == i)
        loaded.ntl_active_team_profile = -1;
      else if (loaded.ntl_active_team_profile > i)
        --loaded.ntl_active_team_profile;
      for (int j = i + 1; j < loaded.ntl_team_profile_count; ++j)
        loaded.ntl_team_profiles[j - 1] = loaded.ntl_team_profiles[j];
      --loaded.ntl_team_profile_count;
      --i;
    }
  }
  loaded.ntl_client_id[8] = 0;
  bool valid_ntl_client_id = strlen(loaded.ntl_client_id) == 8;
  for (int i = 0; valid_ntl_client_id && i < 8; ++i) {
    char c = loaded.ntl_client_id[i];
    valid_ntl_client_id =
        (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  }
  if (!valid_ntl_client_id) loaded.ntl_client_id[0] = 0;

  loaded.ntl_tag_password_md5[32] = 0;
  bool valid_tag_hash = !loaded.ntl_tag_password_md5[0] ||
                        strlen(loaded.ntl_tag_password_md5) == 32;
  for (int i = 0; valid_tag_hash && loaded.ntl_tag_password_md5[0] && i < 32; ++i) {
    char c = loaded.ntl_tag_password_md5[i];
    valid_tag_hash = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  }
  if (!valid_tag_hash) loaded.ntl_tag_password_md5[0] = 0;
  if (loaded.ntl_tag_id < -1 || loaded.ntl_tag_id > 666) {
    loaded.ntl_tag_id = -1;
    loaded.ntl_tag_password_md5[0] = 0;
  }

  loaded.vlither_tag_backend_url[sizeof loaded.vlither_tag_backend_url - 1] = 0;
  loaded.vlither_tag_name[sizeof loaded.vlither_tag_name - 1] = 0;
  if (loaded.vlither_tag_id < -1) loaded.vlither_tag_id = -1;
  if (loaded.vlither_tag_backend_url[0] &&
      strncmp(loaded.vlither_tag_backend_url, "https://", 8) != 0 &&
      strncmp(loaded.vlither_tag_backend_url, "http://", 7) != 0) {
    loaded.vlither_tag_backend_url[0] = 0;
    loaded.vlither_tag_id = -1;
    loaded.vlither_tag_name[0] = 0;
  }

  loaded.show_tags = !!loaded.show_tags;
  loaded.show_ntl_tags = !!loaded.show_ntl_tags;
  loaded.show_vlither_tags = !!loaded.show_vlither_tags;

  if (loaded.background_style < 0 || loaded.background_style > 15)
    loaded.background_style = 0;
  if (loaded.cursor_size < 24 || loaded.cursor_size > 128)
    loaded.cursor_size = 48;
  for (int i = 0; i < MAX_KEY_BTNS; ++i) {
    if (loaded.key_btn_shape[i] > 1) loaded.key_btn_shape[i] = 0;
  }
  if (!isfinite(loaded.tag_size_scale) || loaded.tag_size_scale < 0.50f ||
      loaded.tag_size_scale > 2.00f)
    loaded.tag_size_scale = 1.0f;
  loaded.tag_size_with_zoom = !!loaded.tag_size_with_zoom;
  if (loaded.external_input_mode < 0 || loaded.external_input_mode > 1)
    loaded.external_input_mode = 0;
  loaded.vlither_show_minimap_players = !!loaded.vlither_show_minimap_players;
  loaded.vlither_show_player_stats = !!loaded.vlither_show_player_stats;
  loaded.ntl_show_player_stats = !!loaded.ntl_show_player_stats;
  loaded.voice_chat_enabled = !!loaded.voice_chat_enabled;
  loaded.arrow_sync_with_zoom = !!loaded.arrow_sync_with_zoom;
  /* The old boost animation preference is intentionally retired: selected
     arrow artwork must remain identical during normal movement and boost. */
  loaded.boost_arrow_anim = false;
  if (loaded.arrow_style < 0 || loaded.arrow_style >= ARROW_STYLE_COUNT)
    loaded.arrow_style = 0;
  if (loaded.homepage_background < 0 || loaded.homepage_background > 6)
    loaded.homepage_background = 5;
  if (!isfinite(loaded.homepage_blur) || loaded.homepage_blur < 0.0f ||
      loaded.homepage_blur > 100.0f)
    loaded.homepage_blur = 0.0f;
  loaded.leaderboard_title[sizeof loaded.leaderboard_title - 1] = 0;
  if (!loaded.leaderboard_title[0])
    strcpy(loaded.leaderboard_title, "Vlither Leaderboard");
  loaded.minimap_display_name[sizeof loaded.minimap_display_name - 1] = 0;
  loaded.minimap_show_own_name = !!loaded.minimap_show_own_name;
  loaded.vlither_chat_joined = !!loaded.vlither_chat_joined;
  loaded.own_skin_invisible = !!loaded.own_skin_invisible;
  loaded.custom_arrow_enabled = !!loaded.custom_arrow_enabled;
  for (int i = 0; i < 2; ++i) {
    loaded.food_glow[i] = !!loaded.food_glow[i];
    loaded.center_line_others[i] = !!loaded.center_line_others[i];
    if (!isfinite(loaded.snake_shadow_strength[i]) ||
        loaded.snake_shadow_strength[i] < 0.0f ||
        loaded.snake_shadow_strength[i] > 3.0f)
      loaded.snake_shadow_strength[i] = 1.0f;
  }
  if (loaded.shader_cycle_key < GLFW_KEY_0 ||
      loaded.shader_cycle_key > GLFW_KEY_Z)
    loaded.shader_cycle_key = GLFW_KEY_G;
  if (loaded.invisible_skin_key < GLFW_KEY_0 ||
      loaded.invisible_skin_key > GLFW_KEY_Z)
    loaded.invisible_skin_key = GLFW_KEY_I;
  if (loaded.shader_cycle_index < -1 || loaded.shader_cycle_index > 2)
    loaded.shader_cycle_index = -1;
  if (!isfinite(loaded.zslider_thickness) ||
      loaded.zslider_thickness < 0.35f || loaded.zslider_thickness > 3.0f)
    loaded.zslider_thickness = 1.0f;
  if (!isfinite(loaded.zslider_thumb_scale) ||
      loaded.zslider_thumb_scale < 0.50f || loaded.zslider_thumb_scale > 3.0f)
    loaded.zslider_thumb_scale = 1.0f;
  if (loaded.server_address_filter < 0 || loaded.server_address_filter > 2)
    loaded.server_address_filter = 0;
  loaded.server_address[sizeof loaded.server_address - 1] = 0;
  if (!loaded.server_address[0])
    snprintf(loaded.server_address, sizeof loaded.server_address, "%s",
             loaded.ipv4[0] ? loaded.ipv4 : "148.113.20.151:444");
  loaded.minimap_clock = !!loaded.minimap_clock;
  loaded.ntl_hide_enemy_tags = !!loaded.ntl_hide_enemy_tags;
  loaded.ntl_hide_enemy_cosmetics = !!loaded.ntl_hide_enemy_cosmetics;
  loaded.ntl_high_visibility_skins = !!loaded.ntl_high_visibility_skins;
  loaded.ntl_nicks_plus = !!loaded.ntl_nicks_plus;
  loaded.ntl_names_on_top = !!loaded.ntl_names_on_top;
  loaded.ntl_legacy_option_1 = false;
  loaded.ntl_legacy_option_2 = false;
  loaded.ntl_legacy_option_3 = false;
  loaded.ntl_chat_timestamps = !!loaded.ntl_chat_timestamps;
  loaded.ntl_dynamic_minimap = !!loaded.ntl_dynamic_minimap;
  loaded.ntl_border_indicator = !!loaded.ntl_border_indicator;
  loaded.ntl_skinless_peek = !!loaded.ntl_skinless_peek;
  loaded.ntl_own_true_skin = !!loaded.ntl_own_true_skin;
  loaded.ntl_team_true_skin = !!loaded.ntl_team_true_skin;
  loaded.ntl_stealth_mode = !!loaded.ntl_stealth_mode;
  if (loaded.ntl_graphics_preset < 0 || loaded.ntl_graphics_preset > 4)
    loaded.ntl_graphics_preset = 3;
  if (loaded.ntl_leaderboard_style < 0 || loaded.ntl_leaderboard_style > 2)
    loaded.ntl_leaderboard_style = 1;
  for (int c = 0; c < 4; ++c) {
    if (!isfinite(loaded.ntl_leaderboard_color[c]) ||
        loaded.ntl_leaderboard_color[c] < 0.0f ||
        loaded.ntl_leaderboard_color[c] > 1.0f)
      loaded.ntl_leaderboard_color[c] =
          (const float[]){1.0f, 0.82f, 0.22f, 1.0f}[c];
  }
  for (int i = 0; i < 2; ++i) {
    if (migrate_single_mode_features) {
      loaded.mode_hide_enemy_tags[i] = loaded.ntl_hide_enemy_tags;
      loaded.mode_hide_enemy_cosmetics[i] = loaded.ntl_hide_enemy_cosmetics;
      loaded.mode_high_visibility_skins[i] =
          loaded.ntl_high_visibility_skins;
      loaded.mode_nicks_plus[i] = loaded.ntl_nicks_plus;
      loaded.mode_names_on_top[i] = loaded.ntl_names_on_top;
      loaded.mode_skinless_peek[i] = loaded.ntl_skinless_peek;
      loaded.mode_own_true_skin[i] = loaded.ntl_own_true_skin;
      loaded.mode_team_true_skin[i] = loaded.ntl_team_true_skin;
      loaded.mode_graphics_preset[i] = loaded.ntl_graphics_preset;
    }
    loaded.mode_hide_enemy_tags[i] = !!loaded.mode_hide_enemy_tags[i];
    loaded.mode_hide_enemy_cosmetics[i] =
        !!loaded.mode_hide_enemy_cosmetics[i];
    loaded.mode_high_visibility_skins[i] =
        !!loaded.mode_high_visibility_skins[i];
    loaded.mode_nicks_plus[i] = !!loaded.mode_nicks_plus[i];
    loaded.mode_names_on_top[i] = !!loaded.mode_names_on_top[i];
    loaded.mode_skinless_peek[i] = !!loaded.mode_skinless_peek[i];
    loaded.mode_own_true_skin[i] = !!loaded.mode_own_true_skin[i];
    loaded.mode_team_true_skin[i] = !!loaded.mode_team_true_skin[i];
    if (loaded.mode_graphics_preset[i] < 0 ||
        loaded.mode_graphics_preset[i] > 4)
      loaded.mode_graphics_preset[i] = 3;
  }
  for (int c = 0; c < 3; ++c) {
    if (!isfinite(loaded.head_dot_color[c]) ||
        loaded.head_dot_color[c] < 0.0f ||
        loaded.head_dot_color[c] > 1.0f)
      loaded.head_dot_color[c] = 1.0f;
  }
  loaded.record_gameplay = !!loaded.record_gameplay;
  loaded.screenshot_on_kill = !!loaded.screenshot_on_kill;
  if (loaded.ratings_owner_token[0]) {
    bool valid_token = loaded.ratings_owner_token[64] == 0;
    for (int i = 0; valid_token && i < 64; ++i) {
      char c = loaded.ratings_owner_token[i];
      valid_token = (c >= '0' && c <= '9') ||
                    (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F');
    }
    if (!valid_token) loaded.ratings_owner_token[0] = 0;
  }
  for (int c = 0; c < 4; ++c) {
    if (!isfinite(loaded.leaderboard_title_color[c]) ||
        loaded.leaderboard_title_color[c] < 0.0f ||
        loaded.leaderboard_title_color[c] > 1.0f)
      loaded.leaderboard_title_color[c] =
          (const float[]){1.0f, 0.88f, 0.30f, 0.96f}[c];
  }
  loaded.vlither_profile_color_custom =
      !!loaded.vlither_profile_color_custom;
  for (int c = 0; c < 3; ++c) {
    if (!isfinite(loaded.vlither_profile_color[c]) ||
        loaded.vlither_profile_color[c] < 0.0f ||
        loaded.vlither_profile_color[c] > 1.0f)
      loaded.vlither_profile_color[c] =
          (const float[]){0.35f, 0.68f, 1.0f}[c];
  }
  if (loaded.vlither_profile_emoji < 0 ||
      loaded.vlither_profile_emoji > 10)
    loaded.vlither_profile_emoji = 0;
  loaded.vlither_chat_hud_visible = !!loaded.vlither_chat_hud_visible;
  loaded.ntl_chat_hud_visible = !!loaded.ntl_chat_hud_visible;
  if (!isfinite(loaded.friends_panel_zoom) ||
      loaded.friends_panel_zoom < 0.75f ||
      loaded.friends_panel_zoom > 1.50f)
    loaded.friends_panel_zoom = 1.0f;
  loaded.snakey_rain_enabled = !!loaded.snakey_rain_enabled;
  loaded.snakey_rain_username[sizeof loaded.snakey_rain_username - 1] = 0;
  loaded.snakey_rain_password[sizeof loaded.snakey_rain_password - 1] = 0;
  if (!loaded.snakey_rain_username[0])
    strcpy(loaded.snakey_rain_username, "snakeyuser");
  if (!loaded.snakey_rain_password[0])
    strcpy(loaded.snakey_rain_password, "wormfood");
  if (loaded.snakey_rain_max_bots < 1 ||
      loaded.snakey_rain_max_bots > 1000)
    loaded.snakey_rain_max_bots = 1000;
  loaded.snakey_rain_bot_name[sizeof loaded.snakey_rain_bot_name - 1] = 0;
  loaded.snakey_rain_bot_skin[sizeof loaded.snakey_rain_bot_skin - 1] = 0;
  if (!loaded.snakey_rain_bot_name[0])
    strcpy(loaded.snakey_rain_bot_name, "SnakeyRain");
  if (!loaded.snakey_rain_bot_skin[0])
    strcpy(loaded.snakey_rain_bot_skin,
           "uuuuuuuauuuuuuaauuuuuaaauuuuaaaauuuaaaaauuaaaaaauaaaaaaa"
           "uuaaaaaauuuaaaaauuuuaaaauuuuuaaauuuuuuaa");

  *usr_settings = loaded;
}

void save_user_settings(user_settings* usr_settings) {
#ifdef ANDROID
  char _settings_path[512];
#endif
  FILE* f = OPEN_SETTINGS_FILE("wb");
  if (f == NULL) {

#ifdef ANDROID
    return;
#else
    printf("Error saving settings.");
    exit(-1);
#endif
  }

  fwrite(usr_settings, sizeof(user_settings), 1, f);
  fclose(f);
}
