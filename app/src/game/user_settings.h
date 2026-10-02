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

  /* v2.4 extension fields. These stay at the end so v2.3 binary settings can
     be loaded as a compatible prefix without resetting player preferences. */
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

  /* v2.5 extension fields. */
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

  /* v2.5.4 extension field. NTL identifies one client by the first eight
     hexadecimal characters of its transmitted nickname. Keep that prefix
     stable so nickname changes update the existing player instead of creating
     a nameless/new entry. */
  char ntl_client_id[9];

  /* NTL tag selection. Public tags are also embedded in the normal Slither
     skin packet; protected tags use the NTL tag mapping service. */
  int ntl_tag_id;
  char ntl_tag_password_md5[33];

  /* Vlither-only backend tags. The URL field is retained only for binary
     settings compatibility; current builds use the official hardcoded
     Vlither backend. The selected assignment is persisted against
     ntl_client_id. */
  char vlither_tag_backend_url[192];
  int vlither_tag_id;
  char vlither_tag_name[64];

  /* Tag visibility controls. The master switch hides both systems without
     disconnecting either service or clearing the player's selected tags. */
  bool show_tags;
  bool show_ntl_tags;
  bool show_vlither_tags;

  /* v2.6 extension fields. */
  int background_style;
  uint8_t key_btn_shape[MAX_KEY_BTNS];

  /* Shared NTL/Vlither tag display editor. */
  float tag_size_scale;
  bool tag_size_with_zoom;

  /* v2.8 extension. 0 = touchscreen controls, 1 = real Android
     USB/Bluetooth mouse + physical keyboard controls. Keep this as an int so
     the appended settings generation grows beyond the previous struct's tail
     padding and old settings files can be distinguished safely. */
  int external_input_mode;

  /* v2.9 extension: player telemetry/minimap controls. These stay at the end
     so older user.dat files remain a compatible prefix. */
  bool vlither_show_minimap_players;
  bool vlither_show_player_stats;
  bool ntl_show_player_stats;

  /* v3.0 extension: previously the player's Voice Chat on/off choice and
     whether the in-game voice status icons were hidden. Vlither Voice has
     been removed, so neither field is read or written anymore — both stay
     declared, unused, purely to keep every later version's offsetof-based
     binary layout (v3.1 through v4.7) compatible with existing user.dat
     files. Do not repurpose or remove them. */
  bool voice_chat_enabled;
  uint8_t voice_settings_reserved[15];
  bool voice_status_icons_hidden;

  /* v3.1 extension: touch-arrow zoom behaviour and the local head-dot
     appearance. The reserve starts after v3.0's last logical byte so an old
     settings file's compiler tail padding cannot overwrite these defaults. */
  uint8_t arrow_head_settings_reserved[16];
  bool arrow_sync_with_zoom;
  vec3 head_dot_color;

  /* v3.2 extension: selected fixed-colour touch-arrow artwork. Reserve a
     clean boundary so an older file's tail padding cannot select garbage. */
  uint8_t arrow_style_settings_reserved[16];
  int arrow_style;

  /* v3.3 extension: homepage-only scene and user-controlled blur. The
     reserve protects defaults from the previous layout's compiler padding. */
  uint8_t homepage_settings_reserved[16];
  int homepage_background;
  float homepage_blur;

  /* v3.4 extension: Vlither HUD/render/network customization. Everything is
     appended so every earlier user.dat remains a compatible prefix. */
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
  int server_address_filter; /* 0 = both, 1 = IPv4, 2 = IPv6 */
  /* Full IPv4/IPv6 authority. The original ipv4 field stays fixed-size so
     existing user.dat files keep their binary layout. */
  char server_address[MAX_SERVER_IP_LEN + 1];

  /* v3.6 extension: optional clock-style minimap overlay. Reserve a new
     boundary so v4.7.1 tail padding cannot accidentally enable it. */
  uint8_t clock_map_settings_reserved[16];
  bool minimap_clock;

  /* v3.7 extension: NTL-inspired competitive visibility and HUD controls.
     Keep this append-only and behind a reserve boundary so older user.dat
     files retain every existing preference while these options use defaults. */
  uint8_t ntl_competitive_settings_reserved[16];
  bool ntl_hide_enemy_tags;
  bool ntl_hide_enemy_cosmetics;
  bool ntl_high_visibility_skins;
  bool ntl_nicks_plus;
  bool ntl_names_on_top;
  /* Reserved slots keep the existing user.dat layout compatible. */
  bool ntl_legacy_option_1;
  bool ntl_legacy_option_2;
  bool ntl_legacy_option_3;
  bool ntl_chat_timestamps;
  bool ntl_dynamic_minimap;
  bool ntl_border_indicator;
  bool ntl_skinless_peek;
  bool ntl_own_true_skin;
  bool ntl_team_true_skin;
  int ntl_graphics_preset;    /* 0=custom, 1=competitive, 2=low, 3=normal, 4=high */
  int ntl_leaderboard_style; /* 0=normal, 1=gradient, 2=single colour */
  vec4 ntl_leaderboard_color;
  bool ntl_stealth_mode;

  /* v3.8 extension: rendering controls now belong to Normal and Assist
     independently. The earlier single-value fields remain above solely for
     settings-file compatibility with the first feature-pack build. */
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

  /* v3.9 extension: local-only Android capture preferences. Recording still
     requires Android's system MediaProjection consent for every new session;
     neither videos nor screenshots are uploaded to a backend. */
  uint8_t capture_settings_reserved[16];
  bool record_gameplay;
  bool screenshot_on_kill;

  /* v4.0 extension: anonymous review ownership. The random token stays only
     on this device; the Vlither backend stores a one-way hash. */
  uint8_t ratings_settings_reserved[16];
  char ratings_owner_token[65];
  long long ratings_admin_reply_seen_ms;

  /* v4.1 extension: the centred "Vlither Leaderboard" title has its own
     colour, independent of the player-row colour style. */
  uint8_t leaderboard_title_settings_reserved[16];
  vec4 leaderboard_title_color;

  /* v4.2 extension: the public Vlither Chat profile used by presence,
     messages and same-server minimap markers. The badge is an index into a
     small font-safe preset list; no arbitrary text is persisted or sent. */
  uint8_t vlither_profile_settings_reserved[16];
  bool vlither_profile_color_custom;
  vec3 vlither_profile_color;
  int vlither_profile_emoji;

  /* v4.3 extension: remember whether each gameplay chat HUD is visible.
     These are display-only preferences and do not leave Global Chat. */
  uint8_t chat_hud_settings_reserved[16];
  bool vlither_chat_hud_visible;
  bool ntl_chat_hud_visible;

  /* v4.4 extension: shared scale for the floating and embedded
     Online Players/Friends lists. */
  uint8_t friends_panel_settings_reserved[16];
  float friends_panel_zoom;

  /* v4.5+ extension: native Snakey Rain integration. Can be toggled live
     without restarting Vlither. Bot in-game name/skin are sent on login. */
  uint8_t snakey_rain_settings_reserved[16];
  bool snakey_rain_enabled;
  char snakey_rain_username[64];
  char snakey_rain_password[64];
  int snakey_rain_max_bots;
  char snakey_rain_bot_name[25];
  char snakey_rain_bot_skin[128];

  /* v4.6 extension: persistent 16-char client identifier ("player ID") sent
     once per connection to Battledome ID-target servers (see
     server_is_bd_id_target/server_bd_id_should_send), right after the
     riddle answer and before the nick/skin combo packet. 8 random bytes
     hex-encoded to 16 lowercase hex characters — same convention as
     ntl_client_id above — generated once via ensure_bd_client_id() and
     reused for this install's lifetime. Sent on the wire as-is (16 ASCII
     bytes), and is exactly what !id prints for copy-paste. */
  uint8_t bd_id_settings_reserved[16];
  char bd_client_id[17];

  /* v4.7 extension: instant-death (skip the post-death spectator view and
     return to the homepage immediately) and showing your own snake's
     nickname above your head in-game, matching how other players' names
     already display. Reserve a clean boundary so an older user.dat's tail
     padding cannot enable or disable either unexpectedly. */
  uint8_t instant_death_settings_reserved[16];
  bool instant_death;
  bool show_own_nickname_ingame;

  /* v4.8 extension: per-origin food size multipliers. Each stacks with the
     existing per-mode food_scale (which still scales every food type at
     once) rather than replacing it: on-screen size = food_scale *
     the multiplier matching that pellet's origin. Indexed the same way as
     food_scale, food_glow, etc. (index 0 = Normal mode, 1 = Assist mode). */
  uint8_t food_origin_scale_settings_reserved[16];
  float death_food_scale[2];
  float normal_food_scale[2];
  float boost_food_scale[2];
} user_settings;

void user_settings_default(user_settings* usr_settings);
void read_user_settings(user_settings* usr_settings);
void save_user_settings(user_settings* usr_settings);

/* Generates a 16-char hex bd_client_id the first time it's needed (on
   install it's empty; this fills it in) and persists it immediately.
   Idempotent — safe to call on every use, mirrors ntl_ensure_client_id. */
void ensure_bd_client_id(user_settings* usr_settings);

#endif
