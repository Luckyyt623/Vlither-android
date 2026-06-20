#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>

#include "../constants.h"

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
  float food_scale;
  float qsm;
  float bg_scale;
  float boost_strength;
  vec3 food_color;
} gameplay_mode;


typedef struct custom_key_btn {
  bool  active;           /* slot is used                              */
  int   glfw_key;         /* GLFW_KEY_* value                          */
  char  label[8];         /* display label e.g. "H", "Boost", "Esc"   */
  float rel_x;            /* centre X as fraction of screen width      */
  float rel_y;            /* centre Y as fraction of screen height     */
  float rel_size;         /* button size as fraction of screen height  */
  float opacity;          /* 0.0 - 1.0                                 */
} custom_key_btn;

typedef struct ntl_team_profile {
  char name[33];
  char team_id[33];
  char auth_key[65];
} ntl_team_profile;

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

  // global settings:
  vec3 bd_color;
  vec4 laser_color;
  int laser_thickness;
  int cursor_size;
  int minimap_size;
  bool restart_rc;
  bool quit_mc;
  bool vsync;
  bool smooth_zoom;
  bool snake_scores;
  bool instant_restart;
  float zoom_step;
  int bot_radius_mult;
  int bot_follow_circle_score;

  /* Control mode: false = joystick (default), true = NTL trackpad */
  bool ctrl_mode_trackpad;

  /* Swap layout: false = joystick-left/boost-right (default),
                  true  = joystick-right/boost-left
     In trackpad mode only boost side is swapped. */
  bool ctrl_swap_sides;

  /* ── Custom control layout ──────────────────────────────────── */
  bool  boost_pos_custom;  /* true = use boost_rel_* instead of auto   */
  float boost_rel_x;       /* boost centre X as fraction of sw          */
  float boost_rel_y;       /* boost centre Y as fraction of sh          */
  float boost_rel_size;    /* boost radius as fraction of sh            */
  float boost_opacity;     /* 0.0 – 1.0                                 */

  bool  joy_pos_custom;
  float joy_rel_x;
  float joy_rel_y;
  float joy_rel_size;
  float joy_opacity;

  /* ── Zoom slider ────────────────────────────────────────────── */
  float zoom_sensitivity;   /* slider speed multiplier (default 1.0)    */
  float zslider_rel_x;      /* centre X fraction of sw                  */
  float zslider_rel_y;      /* centre Y fraction of sh (vertical mode)  */
  float zslider_rel_h;      /* half-height fraction of sh               */
  float zslider_opacity;    /* 0.0 – 1.0                                */
  bool  zslider_horizontal; /* false = vertical (default), true = horiz */

  /* Which hotkeys show as on-screen tap buttons during gameplay */
  bool  hk_show_btn[NUM_HOTKEYS];

  gameplay_mode modes[2];

  // hotkeys:
  hotkey hotkeys[NUM_HOTKEYS];

  /* on-screen custom key buttons */
  custom_key_btn key_btns[MAX_KEY_BTNS];

  // NTL settings:
  ntl_team_profile ntl_teams[12];
  int ntl_team_count;
  int ntl_active_team_idx;
  char ntl_user_id[9];
  bool show_chat_hud;
  bool show_online_players_hud;
  bool show_player_details_hud;
} user_settings;

void user_settings_default(user_settings* usr_settings);
void read_user_settings(user_settings* usr_settings);
void save_user_settings(user_settings* usr_settings);

#endif