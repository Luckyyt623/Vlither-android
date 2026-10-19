#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>

#include "../constants.h"

#define MAX_NTL_TEAM_PROFILES 12
#define MAX_NTL_TEAM_NAME 47

typedef struct hotkey {
  int key;
  bool active;
  int mode;
  char description[MAX_HOTKEY_DESC_LENGTH + 1];
} hotkey;

typedef struct gameplay_mode {
  bool food_flicker;
  bool food_float;
  bool uniform_food_color;
  bool show_crosshair;
  bool show_boost;
  bool show_shadows;
  bool show_background;
  bool show_accessories;
  bool death_effect;
  bool player_names_outline;
  int food_type;
  int boost_type;
  int render_mode;
  bool transparent_skin;
  bool center_line;
  float food_scale;
  float qsm;
  float bg_scale;
  float boost_strength;
  vec3 food_color;
} gameplay_mode;

typedef struct ntl_team_profile {
  char name[MAX_NTL_TEAM_NAME + 1];
  char team_id[96];
  char auth_key[96];
} ntl_team_profile;

typedef struct custom_key_btn {
  bool  active;
  int   glfw_key;
  char  label[8];
  float rel_x;
  float rel_y;
  float rel_size;
  float opacity;
} custom_key_btn;

typedef struct user_settings {
  char version[4];
  char nickname[MAX_NICKNAME_LEN + 1];
  char ipv4[MAX_IPV4_LEN + 1];
  char skin_code[MAX_SKIN_CODE_LEN + 1];
  uint8_t accessory;
  bool custom_skin;
  uint8_t default_skin;
  int score;
  double play_time;
  int kills;
  font_size ui_font_size;
  font_size lb_font_size;
  font_size snake_names_font_size;
  font_size stats_font_size;

  vec3 bd_color;
  vec4 laser_color;
  int laser_thickness;
  int cursor_size;
  int minimap_size;
  bool minimap_pos_custom;
  float minimap_rel_x;
  float minimap_rel_y;
  bool restart_rc;
  bool quit_mc;
  bool vsync;
  bool smooth_zoom;
  bool snake_scores;
  bool instant_restart;
  float zoom_step;
  int bot_radius_mult;
  int bot_follow_circle_score;

  bool ctrl_mode_trackpad;

  bool ctrl_swap_sides;

  bool  boost_pos_custom;
  float boost_rel_x;
  float boost_rel_y;
  float boost_rel_size;
  float boost_opacity;

  bool  joy_pos_custom;
  float joy_rel_x;
  float joy_rel_y;
  float joy_rel_size;
  float joy_opacity;

  float arrow_size;
  float arrow_sensitivity;
  bool  boost_arrow_anim;
  bool  arrow_invisible;
  bool  bot_vis;

  float zoom_sensitivity;
  float zslider_rel_x;
  float zslider_rel_y;
  float zslider_rel_h;
  float zslider_opacity;
  bool  zslider_horizontal;
  bool  zslider_hidden;

  bool  ntl_enabled;
  char  ntl_team_id[96];
  char  ntl_auth_key[96];

  bool  hk_show_btn[NUM_HOTKEYS];

  gameplay_mode modes[2];

  hotkey hotkeys[NUM_HOTKEYS];

  custom_key_btn key_btns[MAX_KEY_BTNS];

  float transparent_skin_opacity[2];
  bool minimap_drag_enabled;
  int fps_limit;
  bool performance_mode;

  float ntl_chat_rel_x;
  float ntl_chat_rel_y;
  float ntl_chat_rel_w;
  float ntl_chat_rel_h;
  float ntl_players_rel_x;
  float ntl_players_rel_y;
  float ntl_players_rel_w;
  float ntl_players_rel_h;

  bool ntl_chat_minimized;
  bool ntl_show_teammates;
  bool ntl_marker_labels;
  int ntl_marker_shape;
  float ntl_marker_size;
  vec4 ntl_marker_color;
  int own_marker_shape;
  float own_marker_size;
  vec4 own_marker_color;

  int ntl_team_profile_count;
  int ntl_active_team_profile;
  ntl_team_profile ntl_team_profiles[MAX_NTL_TEAM_PROFILES];

  char ntl_client_id[9];

  int ntl_tag_id;
  char ntl_tag_password_md5[33];

  char vlither_tag_backend_url[192];
  int vlither_tag_id;
  char vlither_tag_name[64];

  bool show_tags;
  bool show_ntl_tags;
  bool show_vlither_tags;

  int background_style;
  uint8_t key_btn_shape[MAX_KEY_BTNS];

  float tag_size_scale;
  bool tag_size_with_zoom;

  int external_input_mode;

  bool vlither_show_minimap_players;
  bool vlither_show_player_stats;
  bool ntl_show_player_stats;

  bool voice_chat_enabled;
  uint8_t voice_settings_reserved[15];
  bool voice_status_icons_hidden;

  uint8_t arrow_head_settings_reserved[16];
  bool arrow_sync_with_zoom;
  vec3 head_dot_color;

  uint8_t arrow_style_settings_reserved[16];
  int arrow_style;

  uint8_t homepage_settings_reserved[16];
  int homepage_background;
  float homepage_blur;

  uint8_t vlither_feature_settings_reserved[16];
  char leaderboard_title[33];
  bool food_glow[2];
  bool center_line_others[2];
  float snake_shadow_strength[2];
  char minimap_display_name[MAX_NICKNAME_LEN + 1];
  bool minimap_show_own_name;
  bool vlither_chat_joined;
  int shader_cycle_key;
  int invisible_skin_key;
  int shader_cycle_index;
  bool own_skin_invisible;
  float zslider_thickness;
  float zslider_thumb_scale;
  bool custom_arrow_enabled;
  int server_address_filter;
  char server_address[MAX_SERVER_IP_LEN + 1];

  uint8_t clock_map_settings_reserved[16];
  bool minimap_clock;

  uint8_t ntl_competitive_settings_reserved[16];
  bool ntl_hide_enemy_tags;
  bool ntl_hide_enemy_cosmetics;
  bool ntl_high_visibility_skins;
  bool ntl_nicks_plus;
  bool ntl_names_on_top;
  bool ntl_legacy_option_1;
  bool ntl_legacy_option_2;
  bool ntl_legacy_option_3;
  bool ntl_chat_timestamps;
  bool ntl_dynamic_minimap;
  bool ntl_border_indicator;
  bool ntl_skinless_peek;
  bool ntl_own_true_skin;
  bool ntl_team_true_skin;
  int ntl_graphics_preset;
  int ntl_leaderboard_style;
  vec4 ntl_leaderboard_color;
  bool ntl_stealth_mode;

  uint8_t integrated_mode_settings_reserved[16];
  bool mode_hide_enemy_tags[2];
  bool mode_hide_enemy_cosmetics[2];
  bool mode_high_visibility_skins[2];
  bool mode_nicks_plus[2];
  bool mode_names_on_top[2];
  bool mode_skinless_peek[2];
  bool mode_own_true_skin[2];
  bool mode_team_true_skin[2];
  int mode_graphics_preset[2];

  uint8_t capture_settings_reserved[16];
  bool record_gameplay;
  bool screenshot_on_kill;

  uint8_t ratings_settings_reserved[16];
  char ratings_owner_token[65];
  long long ratings_admin_reply_seen_ms;

  uint8_t leaderboard_title_settings_reserved[16];
  vec4 leaderboard_title_color;

  uint8_t vlither_profile_settings_reserved[16];
  bool vlither_profile_color_custom;
  vec3 vlither_profile_color;
  int vlither_profile_emoji;

  uint8_t chat_hud_settings_reserved[16];
  bool vlither_chat_hud_visible;
  bool ntl_chat_hud_visible;

  uint8_t friends_panel_settings_reserved[16];
  float friends_panel_zoom;

  uint8_t snakey_rain_settings_reserved[16];
  bool snakey_rain_enabled;
  char snakey_rain_username[64];
  char snakey_rain_password[64];
  int snakey_rain_max_bots;
  char snakey_rain_bot_name[25];
  char snakey_rain_bot_skin[128];

  uint8_t bd_id_settings_reserved[16];
  char bd_client_id[17];

  uint8_t instant_death_settings_reserved[16];
  bool instant_death;
  bool show_own_nickname_ingame;

  uint8_t food_origin_scale_settings_reserved[16];
  float death_food_scale[2];
  float normal_food_scale[2];
  float boost_food_scale[2];
} user_settings;

void user_settings_default(user_settings* usr_settings);
void read_user_settings(user_settings* usr_settings);
void save_user_settings(user_settings* usr_settings);

void ensure_bd_client_id(user_settings* usr_settings);

#endif
