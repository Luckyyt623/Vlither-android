#include "title_screen.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#include "../android_jni.h"
#endif

#include "../network/server.h"
#include "../user.h"
#include "../game/vlither_tags.h"
#include "../game/snakey_rain.h"
#include "../game/lifetime_stats.h"
#include "../imgui_setup.h"
#include "ratings.h"
#include "leaderboard.h"
#include "../game/vlither_ratings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool g_sl_popup_open = false;

static int g_privacy_section = 0;

static long long event_now_ms(void) {
  return (long long)time(NULL) * 1000LL;
}

static const char* const HOMEPAGE_BACKGROUND_NAMES[] = {
    "Normal Vlither", "Alpine Valley", "Himalayan Dawn",
    "Cloudsea Sunrise", "Neon Bridge", "Galaxy", "Fuji Sunset",
    "Custom (Upload)",
};

static ImVec4 homepage_accent(int background) {
  static const ImVec4 accents[] = {
      {0.62f, 0.52f, 0.98f, 1.0f},  /* Normal Vlither   - violet  */
      {0.36f, 0.82f, 0.48f, 1.0f},  /* Alpine Valley    - green   */
      {0.98f, 0.66f, 0.34f, 1.0f},  /* Himalayan Dawn   - amber   */
      {0.30f, 0.70f, 1.00f, 1.0f},  /* Cloudsea Sunrise - sky     */
      {0.92f, 0.40f, 0.98f, 1.0f},  /* Neon Bridge      - magenta */
      {0.40f, 0.55f, 1.00f, 1.0f},  /* Galaxy           - indigo  */
      {0.97f, 0.72f, 0.28f, 1.0f},  /* Fuji Sunset      - gold    */
      {0.82f, 0.84f, 0.90f, 1.0f},  /* Custom           - silver  */
  };
  if (background < 0 || background > 7) background = 5;
  return accents[background];
}

static void push_homepage_theme(int background) {
  ImVec4 c = homepage_accent(background);
  igPushStyleColor_Vec4(ImGuiCol_FrameBg,      (ImVec4){0.05f, 0.05f, 0.055f, 0.92f});
  igPushStyleColor_Vec4(ImGuiCol_Button,       (ImVec4){0.04f, 0.04f, 0.045f, 0.82f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,(ImVec4){0.05f + c.x * 0.16f, 0.05f + c.y * 0.16f, 0.05f + c.z * 0.16f, 0.90f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.07f + c.x * 0.30f, 0.07f + c.y * 0.30f, 0.07f + c.z * 0.30f, 0.95f});
  igPushStyleColor_Vec4(ImGuiCol_Border,       (ImVec4){1.0f, 1.0f, 1.0f, 0.12f});
  igPushStyleColor_Vec4(ImGuiCol_PopupBg,      (ImVec4){0.08f + c.x * 0.03f, 0.075f + c.y * 0.03f, 0.07f + c.z * 0.03f, 0.97f});
}

static void apply_homepage_background(tenv* env) {
  if (!env || !env->usr || !env->usr->r || !env->ctx) return;
  tuser_data* usr = env->usr;
  renderer* r = usr->r;
  int selected = usr->usrs.homepage_background;
  if (selected < 0 || selected > 7) selected = 5;

  r->global.bd_opacity = 0.0f;
  r->global.minimap_opacity = 0.0f;
  r->global.bg_blur = 0.0f;
  r->global.bg_color[0] = 1.0f;
  r->global.bg_color[1] = 1.0f;
  r->global.bg_color[2] = 1.0f;

  if (selected == 0) {
    renderer_set_background_variant(r, env->ctx, 0);
    r->global.bg_opacity = 0.0f;
    return;
  }

  int homepage_variant;
  float brightness;
  if (selected == 7) {
    homepage_variant = 23;
    brightness = 1.0f;
  } else {
    homepage_variant = 16 + selected;
    static const float scene_brightness[] = {
        1.00f, 0.68f, 0.74f, 0.76f, 0.76f, 0.88f, 0.72f};
    brightness = scene_brightness[selected];
  }
  renderer_set_background_variant(r, env->ctx, homepage_variant);
  if (r->bg_variant != homepage_variant || !r->active_bg_tex ||
      r->active_bg_tex->size[0] <= 0 || r->active_bg_tex->size[1] <= 0) {
    r->global.bg_opacity = 0.0f;
    return;
  }

  const float scale_x = (float)env->ctx->size[0] /
                        (float)r->active_bg_tex->size[0];
  const float scale_y = (float)env->ctx->size[1] /
                        (float)r->active_bg_tex->size[1];
  const float cover_scale = fmaxf(scale_x, scale_y);

  r->global.zoom = 1.0f;
  r->global.bg_scale = cover_scale;
  r->global.view[0] = r->active_bg_tex->size[0] * cover_scale * 0.5f;
  r->global.view[1] = r->active_bg_tex->size[1] * cover_scale * 0.5f;
  r->global.bg_blur = usr->usrs.homepage_blur / 100.0f;
  r->global.bg_color[0] = brightness;
  r->global.bg_color[1] = brightness;
  r->global.bg_color[2] = brightness;
  r->global.bg_opacity = 1.0f;
}

static float homepage_px(float value) {
  return value * imgui_get_ui_scale();
}

static float homepage_min(float a, float b) { return a < b ? a : b; }
static float homepage_max(float a, float b) { return a > b ? a : b; }

/* ===== glass / accent homepage style helpers ===== */
static float g_hp_u = 1.0f;            /* design-unit -> pixel scale             */
static float g_ox = 0.0f, g_oy = 0.0f; /* pixel offset of the design origin      */
static bool  g_hp_portrait = false;
static float g_hp_w = 0.0f;            /* screen width in pixels                 */
static ImVec4 g_acc = {0.97f, 0.72f, 0.28f, 1.0f}; /* accent for this background */
static float g_tog_x[3], g_tog_y[3], g_tog_w_p;

static float hp_toggle_w(void) {
  return g_hp_portrait ? g_tog_w_p : 420.0f * g_hp_u;
}
static ImVec2 hp_toggle_cursor(int idx, float bw) {
  if (g_hp_portrait) return (ImVec2){g_tog_x[idx], g_tog_y[idx]};
  static const float ys[3] = {88.0f, 170.0f, 250.0f};
  return (ImVec2){g_hp_w - bw - 16.0f * g_hp_u, ys[idx] * g_hp_u};
}

static void hp_knob(bool on) {
  ImVec2 a, b;
  igGetItemRectMin(&a);
  igGetItemRectMax(&b);
  float h = b.y - a.y;
  float th = h * 0.62f, tw = th * 1.9f;
  float right = b.x - h * 0.28f;
  ImVec2 tmin = {right - tw, a.y + (h - th) * 0.5f};
  ImVec2 tmax = {right, tmin.y + th};
  ImDrawList* dl = igGetWindowDrawList();
  ImDrawList_AddRectFilled(dl, tmin, tmax,
      igColorConvertFloat4ToU32((ImVec4){0.20f, 0.18f, 0.15f, 0.95f}), th * 0.5f, 0);
  float kr = th * 0.5f - 3.0f * g_hp_u;
  ImVec2 kc = {on ? tmax.x - th * 0.5f : tmin.x + th * 0.5f, tmin.y + th * 0.5f};
  ImVec4 kcol = on ? g_acc : (ImVec4){0.72f, 0.66f, 0.60f, 1.0f};
  ImDrawList_AddCircleFilled(dl, kc, kr, igColorConvertFloat4ToU32(kcol), 24);
}

static void draw_homepage_background_picker(tenv* env, float frame_height) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  int selected = usrs->homepage_background;
  if (selected < 0 || selected > 7) selected = 5;

  char button_label[80];
  snprintf(button_label, sizeof button_label,
           "Background: %s##homepage_background_button",
           HOMEPAGE_BACKGROUND_NAMES[selected]);
  ImVec2 label_size;
  igCalcTextSize(&label_size, button_label,
                 strstr(button_label, "##"), true, -1.0f);
  float button_w = homepage_max(hp_toggle_w(),
                                label_size.x + style->FramePadding.x * 2.0f + 90.0f * g_hp_u);
  button_w = homepage_min(button_w,
                          env->ctx->size[0] - homepage_px(32.0f));
    igSetCursorPos(hp_toggle_cursor(0, button_w));
  if (igButton(button_label, (ImVec2){button_w, 64.0f * g_hp_u}))
    igOpenPopup_Str("Homepage Background", 0);
  hp_knob(false);

  float popup_w = homepage_min(homepage_px(350.0f),
                               env->ctx->size[0] - homepage_px(24.0f));
  igSetNextWindowSize((ImVec2){popup_w, 0.0f}, ImGuiCond_Appearing);
  if (!igBeginPopup("Homepage Background", ImGuiWindowFlags_NoSavedSettings))
    return;

  igSeparatorText("Homepage Background");
  igTextWrapped("The selected scene is previewed live. Only one photo is kept in memory.");
  igSpacing();
  igSetNextItemWidth(-1.0f);
  if (igCombo_Str_arr("##homepage_scene", &usrs->homepage_background,
                      HOMEPAGE_BACKGROUND_NAMES, 8, 8))
    save_user_settings(usrs);

  igBeginDisabled(usrs->homepage_background == 0);
  igText("Background blur");
  igSetNextItemWidth(-1.0f);
  igSliderFloat("##homepage_blur", &usrs->homepage_blur, 0.0f, 100.0f,
                "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
  if (igIsItemDeactivatedAfterEdit()) save_user_settings(usrs);
  igEndDisabled();

  igSpacing();
  ImVec2 avail;
  igGetContentRegionAvail(&avail);
  float half = (avail.x - style->ItemSpacing.x) * 0.5f;
  if (igButton("Normal Vlither", (ImVec2){half, 0.0f})) {
    usrs->homepage_background = 0;
    save_user_settings(usrs);
  }
  igSameLine(0, style->ItemSpacing.x);
  if (igButton("Default Galaxy", (ImVec2){half, 0.0f})) {
    usrs->homepage_background = 5;
    save_user_settings(usrs);
  }
#ifdef ANDROID
  igSpacing();
  if (igButton("Upload from gallery", (ImVec2){-1, 0.0f})) {
    if (android_jni_request_custom_background()) {
      usrs->homepage_background = 7;
      save_user_settings(usrs);
    }
  }
  igTextWrapped("Pick a photo or a GIF from your phone. GIFs animate; "
                "it's shared with the skin editor and the in-game "
                "\"Custom (Upload)\" background too.");
#endif
  igEndPopup();
}

static void draw_snakey_rain_homepage(tenv *env, float frame_height) {
  if (!env || !env->usr || !env->ctx) return;
  user_settings *us = &env->usr->usrs;
    const float button_w = hp_toggle_w();
  char label[96];
  snprintf(label, sizeof label, "Snakey Rain: %s##snakey_rain_home",
           us->snakey_rain_enabled ? "ON" : "OFF");
  igSetCursorPos(hp_toggle_cursor(1, button_w));
  if (igButton(label, (ImVec2){button_w, 64.0f * g_hp_u}))
    igOpenPopup_Str("Snakey Rain Settings", 0);
  hp_knob(us->snakey_rain_enabled);

  float popup_w = homepage_min(homepage_px(390.0f),
                               env->ctx->size[0] - homepage_px(24.0f));
  igSetNextWindowSize((ImVec2){popup_w, 0.0f}, ImGuiCond_Appearing);
  static bool s_cred_ok = false;   /* user accepted the "real account" notice */
  if (!igBeginPopup("Snakey Rain Settings", ImGuiWindowFlags_NoSavedSettings)) {
    s_cred_ok = false;             /* ask again next time the panel is opened */
    return;
  }

  igSeparatorText("Snakey Rain");
  if (igCollapsingHeader_TreeNodeFlags("How to use", ImGuiTreeNodeFlags_DefaultOpen)) {
    igTextWrapped("1. Snakey Rain must be ON (enable it below).");
    igTextWrapped("2. Join inactive servers (where fewer people are playing).");
    igTextWrapped("3. Click the \"Bots\" button at the top.");
    igSpacing();
  }
  bool enabled = us->snakey_rain_enabled;
  if (igCheckbox("Enable Snakey Rain", &enabled)) {
    us->snakey_rain_enabled = enabled;
    save_user_settings(us);
    snakey_rain_apply_settings(env);
  }
  igSpacing();

  /* The default login (snakeyuser / wormfood) cannot spawn bots. Editing it
   * first shows a notice and needs an OK. */
  const bool cred_default =
      strcmp(us->snakey_rain_username, "snakeyuser") == 0 ||
      strcmp(us->snakey_rain_password, "wormfood") == 0;
  const bool cred_locked = cred_default && !s_cred_ok;
  const ImGuiInputTextFlags cred_flags =
      cred_locked ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None;

  igBeginDisabled(!us->snakey_rain_enabled);
  igSetNextItemWidth(-1.0f);
  igInputTextWithHint("##snakey_username", "Snakey Rain username",
                      us->snakey_rain_username,
                      sizeof us->snakey_rain_username,
                      cred_flags, NULL, NULL);
  if (cred_locked && igIsItemClicked(0))
    igOpenPopup_Str("Snakey Rain ID notice", 0);
  if (igIsItemDeactivatedAfterEdit()) {
    save_user_settings(us);
    snakey_rain_apply_settings(env);
  }
  igSetNextItemWidth(-1.0f);
  igInputTextWithHint("##snakey_password", "Snakey Rain password",
                      us->snakey_rain_password,
                      sizeof us->snakey_rain_password,
                      ImGuiInputTextFlags_Password | cred_flags, NULL, NULL);
  if (cred_locked && igIsItemClicked(0))
    igOpenPopup_Str("Snakey Rain ID notice", 0);
  if (igIsItemDeactivatedAfterEdit()) {
    save_user_settings(us);
    snakey_rain_apply_settings(env);
  }
  igText("Maximum bots");
  igSetNextItemWidth(-1.0f);
  igSliderInt("##snakey_max_bots", &us->snakey_rain_max_bots, 1, 1000,
              "%d", ImGuiSliderFlags_AlwaysClamp);
  if (igIsItemDeactivatedAfterEdit()) {
    save_user_settings(us);
    snakey_rain_apply_settings(env);
  }
  igText("Bot in-game name (max 24)");
  igSetNextItemWidth(-1.0f);
  igInputTextWithHint("##snakey_bot_name", "Name shown on bots",
                      us->snakey_rain_bot_name,
                      sizeof us->snakey_rain_bot_name,
                      ImGuiInputTextFlags_None, NULL, NULL);
  if (igIsItemDeactivatedAfterEdit()) {
    save_user_settings(us);
    snakey_rain_apply_settings(env);
  }
  igText("Bot skin code");
  igSetNextItemWidth(-1.0f);
  igInputTextWithHint("##snakey_bot_skin", "Skin string (same as extension)",
                      us->snakey_rain_bot_skin,
                      sizeof us->snakey_rain_bot_skin,
                      ImGuiInputTextFlags_None, NULL, NULL);
  if (igIsItemDeactivatedAfterEdit()) {
    save_user_settings(us);
    snakey_rain_apply_settings(env);
  }
  igEndDisabled();

  if (cred_locked && us->snakey_rain_enabled)
    igTextDisabled("Tap username or password to change them.");

  igSetNextWindowPos((ImVec2){env->ctx->size[0] * 0.5f, env->ctx->size[1] * 0.5f},
                     ImGuiCond_Appearing, (ImVec2){0.5f, 0.5f});
  if (igBeginPopupModal("Snakey Rain ID notice", NULL,
                        ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoSavedSettings)) {
    igPushTextWrapPos(homepage_px(320.0f));
    igTextWrapped("Use real account id otherwise bot will not spawn.");
    igPopTextWrapPos();
    igSpacing();
    if (igButton("OK", (ImVec2){homepage_px(150.0f), 0.0f})) {
      s_cred_ok = true;
      igCloseCurrentPopup();
    }
    igSameLine(0.0f, -1.0f);
    if (igButton("Cancel", (ImVec2){homepage_px(150.0f), 0.0f}))
      igCloseCurrentPopup();
    igEndPopup();
  }

  igSpacing();
  igTextWrapped("Credentials, bot name/skin, and the selected game server are sent to snakeyrain.com when bots start.");
  if (snakey_rain_enabled_at_start()) {
    igTextColored((ImVec4){0.35f, 1.0f, 0.55f, 1.0f},
                  "Active now.");
  } else if (us->snakey_rain_enabled) {
    igTextColored((ImVec4){1.0f, 0.85f, 0.35f, 1.0f},
                  "Enabled — will connect when you play.");
  }

  ImVec2 avail;
  igGetContentRegionAvail(&avail);
  if (igButton("Close", (ImVec2){avail.x, 0.0f})) igCloseCurrentPopup();
  igEndPopup();
}

static void event_format_local_time(long long timestamp_ms,
                                    char *out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (timestamp_ms <= 0) {
    snprintf(out, cap, "Not set");
    return;
  }
  time_t seconds = (time_t)(timestamp_ms / 1000LL);
  struct tm local_value;
#ifdef _WIN32
  localtime_s(&local_value, &seconds);
#else
  localtime_r(&seconds, &local_value);
#endif
  if (!strftime(out, cap, "%a, %d %b %Y - %I:%M %p", &local_value))
    snprintf(out, cap, "Time unavailable");
}

static void event_format_countdown(long long remaining_ms,
                                   char *out, size_t cap) {
  if (!out || cap == 0) return;
  long long total = remaining_ms > 0 ? remaining_ms / 1000LL : 0;
  long long days = total / 86400LL;
  long long hours = (total % 86400LL) / 3600LL;
  long long minutes = (total % 3600LL) / 60LL;
  long long seconds = total % 60LL;
  if (days > 0)
    snprintf(out, cap, "%lldd %02lld:%02lld:%02lld",
             days, hours, minutes, seconds);
  else
    snprintf(out, cap, "%02lld:%02lld:%02lld", hours, minutes, seconds);
}

static void join_server_address(tenv *env, const char *server_ip) {
  if (!env || !env->usr) return;
  if (!server_ip || !server_ip[0]) return;
  tuser_data *usr = env->usr;
  strncpy(usr->usrs.server_address, server_ip, MAX_SERVER_IP_LEN);
  usr->usrs.server_address[MAX_SERVER_IP_LEN] = 0;
  save_user_settings(&usr->usrs);
  usr->gdata.conn = CONNECTING;
  usr->gdata.curr_screen = PLAYING;
  glfwSetTime(0);
  server_connect(env);
}

static void join_event(tenv *env, int index) {
  join_server_address(env, vlither_event_server_at(index));
}

/* First http(s) link inside an event description (registration, bot channel). */
static bool event_find_link(const char *text, char *out, size_t cap) {
  const char *p = text ? strstr(text, "https://") : NULL;
  if (!p) return false;
  size_t n = 0;
  while (p[n] && p[n] != ' ' && p[n] != '\n' && p[n] != ')' &&
         p[n] != '>' && n + 1 < cap) {
    out[n] = p[n];
    ++n;
  }
  out[n] = 0;
  return n > 8;
}

static ImVec4 ntl_tier_color(const char *tier) {
  if (!strcmp(tier, "high")) return (ImVec4){1.0f, 0.84f, 0.22f, 1.0f};
  if (!strcmp(tier, "medium")) return (ImVec4){0.38f, 0.86f, 1.0f, 1.0f};
  return (ImVec4){0.78f, 0.80f, 0.84f, 1.0f};
}

/* One card. state: 0 = live, 1 = upcoming, 2 = ended */
static void ntl_event_card(tenv *env, int i, int state, long long now_ms) {
  ImGuiStyle *style = igGetStyle();
  (void)style;
  igPushID_Int(i);
  const long long start_ms = vlither_ntl_event_start_at_ms(i);
  const long long end_ms = vlither_ntl_event_end_at_ms(i);
  char title[128], start_text[96], end_text[96], countdown[64], prize[96];
  char date_only[32] = "";
  event_format_local_time(start_ms, start_text, sizeof start_text);
  event_format_local_time(end_ms, end_text, sizeof end_text);
  if (start_ms > 0) {
    time_t sec = (time_t)(start_ms / 1000LL);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &sec);
#else
    localtime_r(&sec, &lt);
#endif
    strftime(date_only, sizeof date_only, "%d %b", &lt);
  }
  int num = vlither_ntl_event_number_at(i);
  if (num >= 0)
    snprintf(title, sizeof title, "%s #%d  -  %s",
             vlither_ntl_event_type_at(i), num, date_only);
  else
    snprintf(title, sizeof title, "%s  -  %s", vlither_ntl_event_type_at(i),
             date_only);
  igSeparatorText(title);

  if (state == 0)
    igTextColored((ImVec4){0.20f, 0.92f, 0.55f, 1.0f}, "LIVE NOW");
  else if (state == 1) {
    event_format_countdown(start_ms - now_ms, countdown, sizeof countdown);
    igTextColored((ImVec4){0.95f, 0.78f, 0.28f, 1.0f}, "Starts in %s",
                  countdown);
  } else {
    igTextDisabled("Ended");
  }
  igText("Start: %s", start_text);
  if (end_ms > 0) {
    igText("End: %s", end_text);
    long long mins = (end_ms - start_ms) / 60000LL;
    if (mins > 0) igText("Duration: %lld min", mins);
  }
  if (vlither_ntl_event_people_at(i) > 0)
    igText("Players: %d", vlither_ntl_event_people_at(i));
  if (vlither_ntl_event_prize_text_at(i, prize, sizeof prize)) {
    const char *tier = vlither_ntl_event_tier_at(i);
    igTextColored(ntl_tier_color(tier), "Prize: %s%s%s%s", prize,
                  tier[0] ? "  [" : "", tier, tier[0] ? "]" : "");
  }
  const char *server = vlither_ntl_event_server_at(i);
  if (server[0]) igText("Server: %s", server);

  const char *desc = vlither_ntl_event_description_at(i);
  if (desc[0]) {
    if (igCollapsingHeader_TreeNodeFlags("Details & Rules", 0)) {
      igPushTextWrapPos(0.0f);
      igTextWrapped("%s", desc);
      igPopTextWrapPos();
      char link[200];
      if (event_find_link(desc, link, sizeof link) &&
          igButton("Open registration link", (ImVec2){0, 0}))
        android_jni_open_url(link);
    }
  }
  if (server[0] && state != 2) {
    if (igButton(state == 0 ? "Join Event" : "Join early",
                 (ImVec2){homepage_px(180.0f), 0}))
      join_server_address(env, server);
    igSameLine(0, -1);
    if (igButton("Copy server", (ImVec2){homepage_px(150.0f), 0}))
      igSetClipboardText(server);
  }
  igSpacing();
  igPopID();
}

static void ntl_events_section(tenv *env, long long now_ms) {
  igSpacing();
  igTextColored((ImVec4){0.45f, 0.88f, 0.70f, 1.0f}, "Events");
  igTextDisabled("Live, upcoming and past events.");
  const int total = vlither_ntl_event_count();
  if (total <= 0) {
    igTextWrapped("%s", vlither_ntl_events_status());
    return;
  }
  /* summary */
  int live = 0, upcoming = 0, past = 0, players = 0, counted = 0;
  for (int i = 0; i < total; ++i) {
    long long s = vlither_ntl_event_start_at_ms(i);
    long long e = vlither_ntl_event_end_at_ms(i);
    if (s > now_ms) ++upcoming;
    else if (e <= 0 || now_ms < e) ++live;
    else ++past;
    if (vlither_ntl_event_people_at(i) > 0) {
      players += vlither_ntl_event_people_at(i);
      ++counted;
    }
  }
  igTextDisabled("%d live | %d upcoming | %d past | avg %d players", live,
                 upcoming, past, counted ? players / counted : 0);

  if (igBeginTabBar("##ntl_event_tabs", 0)) {
    if (igBeginTabItem("Live & Upcoming", NULL, 0)) {
      bool any = false;
      for (int i = 0; i < total; ++i) {
        long long s = vlither_ntl_event_start_at_ms(i);
        long long e = vlither_ntl_event_end_at_ms(i);
        if (s <= now_ms && e > 0 && now_ms >= e) continue;
        if (s <= now_ms) { ntl_event_card(env, i, 0, now_ms); any = true; }
      }
      /* soonest upcoming first (list is newest first) */
      for (int i = total - 1; i >= 0; --i) {
        if (vlither_ntl_event_start_at_ms(i) > now_ms) {
          ntl_event_card(env, i, 1, now_ms);
          any = true;
        }
      }
      if (!any) igTextDisabled("Nothing live or scheduled right now.");
      igEndTabItem();
    }
    if (igBeginTabItem("Past", NULL, 0)) {
      bool any = false;
      for (int i = 0; i < total; ++i) {
        long long s = vlither_ntl_event_start_at_ms(i);
        long long e = vlither_ntl_event_end_at_ms(i);
        if (s <= now_ms && e > 0 && now_ms >= e) {
          ntl_event_card(env, i, 2, now_ms);
          any = true;
        }
      }
      if (!any) igTextDisabled("No past events yet.");
      igEndTabItem();
    }
    igEndTabBar();
  }
}

void ui_events_panel(tenv *env) {
  if (!env || !env->usr) return;
  tuser_data *usr = env->usr;
  ImGuiStyle *style = igGetStyle();

  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]->LegacySize);
  igTextColored((ImVec4){0.45f, 0.68f, 1.0f, 1.0f}, "Vlither Events");
  igTextDisabled("Times below use this phone's timezone.");
  ImVec2 nav_avail;
  igGetContentRegionAvail(&nav_avail);
  float nav_w = (nav_avail.x - style->ItemSpacing.x) * 0.5f;
  if (igButton("Back to homepage##events", (ImVec2){nav_w, 0})) {
    usr->gdata.curr_screen = TITLE_SCREEN;
    igPopFont();
    return;
  }
  igSameLine(0, style->ItemSpacing.x);
  if (igButton("Refresh events", (ImVec2){nav_w, 0}))
    vlither_event_refresh();
  igSeparator();

  igBeginChild_Str("##events_full_page_scroll", (ImVec2){0, 0},
                   ImGuiChildFlags_None,
                   ImGuiWindowFlags_AlwaysVerticalScrollbar);
  int count = vlither_event_count();
  if (count <= 0) {
    igTextWrapped("%s", vlither_event_status());
  }
  const long long now_ms = event_now_ms();
  for (int i = 0; i < count; ++i) {
    igPushID_Int(i);
    const long long start_ms = vlither_event_start_at_ms(i);
    const long long end_ms = vlither_event_end_at_ms(i);
    const long long remove_ms = vlither_event_remove_at_ms(i);
    const bool live = start_ms > 0 && now_ms >= start_ms &&
                      (remove_ms <= 0 || now_ms < remove_ms);
    char start_text[96], end_text[96], countdown[64];
    event_format_local_time(start_ms, start_text, sizeof start_text);
    event_format_local_time(end_ms, end_text, sizeof end_text);

    igSeparatorText(vlither_event_name_at(i));
    if (live)
      igTextColored((ImVec4){0.20f, 0.92f, 0.55f, 1.0f}, "LIVE NOW");
    else if (start_ms > now_ms) {
      event_format_countdown(start_ms - now_ms, countdown, sizeof countdown);
      igTextColored((ImVec4){0.95f, 0.78f, 0.28f, 1.0f},
                    "Starts in %s", countdown);
    }
    igText("Country: %s", vlither_event_country_at(i));
    igText("Start: %s", start_text);
    if (end_ms > 0) igText("End: %s", end_text);
    igText("Prize: %s", vlither_event_prize_at(i));
    igText("Server: %s", vlither_event_server_at(i));
    igTextDisabled("%d player%s interested",
                   vlither_event_interested_count_at(i),
                   vlither_event_interested_count_at(i) == 1 ? "" : "s");
    igSpacing();
    igTextColored((ImVec4){0.72f, 0.80f, 0.94f, 1.0f}, "Rules");
    igPushTextWrapPos(0.0f);
    igTextWrapped("%s", vlither_event_rules_at(i));
    igPopTextWrapPos();
    igSpacing();

    bool interested = vlither_event_interested_at(i);
    if (interested)
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.12f, 0.58f, 0.38f, 0.95f});
    if (igButton(interested ? "Interested - ON" : "Interested",
                 (ImVec2){homepage_px(180.0f), 0}))
      vlither_event_set_interested(i, !interested);
    if (interested) igPopStyleColor(1);

    if (interested) {
      igSameLine(0, -1);
      if (igButton("Join Event", (ImVec2){homepage_px(180.0f), 0}))
        join_event(env, i);
    } else {
      igTextDisabled("Turn on Interested to unlock one-tap joining and reminders.");
    }
    igSpacing();
    igPopID();
  }
  ntl_events_section(env, now_ms);
  igEndChild();
  igPopFont();
}

static void draw_privacy_policy_popup(tenv *env) {
  if (!env || !env->usr) return;
  tuser_data *usr = env->usr;
  ImGuiViewport *vp = igGetMainViewport();
  ImGuiStyle *style = igGetStyle();

  float popup_w = vp->WorkSize.x * 0.82f;
  float popup_h = vp->WorkSize.y * 0.78f;
  if (popup_w < homepage_px(330.0f))
    popup_w = vp->WorkSize.x - homepage_px(20.0f);
  if (popup_w > homepage_px(760.0f)) popup_w = homepage_px(760.0f);
  if (popup_h < homepage_px(360.0f))
    popup_h = vp->WorkSize.y - homepage_px(20.0f);
  if (popup_h > homepage_px(680.0f)) popup_h = homepage_px(680.0f);
  igSetNextWindowSize((ImVec2){popup_w, popup_h}, ImGuiCond_Appearing);

  if (!igBeginPopupModal("Privacy & Policy", NULL,
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoCollapse))
    return;

  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]->LegacySize);

  ImVec2 avail;
  igGetContentRegionAvail(&avail);
  float tab_w = avail.x;
  if (igButton("Vlither Tags", (ImVec2){tab_w, 0})) g_privacy_section = 0;
  if (igButton("About Vlither Android", (ImVec2){tab_w, 0})) g_privacy_section = 1;
  igSeparator();

  float close_h = igGetFrameHeight() + style->ItemSpacing.y * 2.0f;
  igBeginChild_Str("##privacy_policy_body", (ImVec2){0, -close_h},
                   ImGuiChildFlags_None,
                   ImGuiWindowFlags_AlwaysVerticalScrollbar);
  igPushTextWrapPos(0.0f);
  if (g_privacy_section == 0) {
    igTextColored((ImVec4){0.45f, 0.75f, 1.0f, 1.0f},
                  "Vlither Tags - Terms & Conditions");
    igSpacing();
    igBulletText("Each user receives one tag upload at no charge.");
    igBulletText("Each additional tag costs $25. This is a one-time fee for that tag and covers it for its lifetime while the service remains available.");
    igBulletText("Abuse of the free-upload allowance is not permitted. This includes creating fake or duplicate accounts to claim extra free tags. If abuse is confirmed, all associated tags will be deleted.");
    igBulletText("Submitting a tag confirms that you have read and accepted these terms.");
    igBulletText("If Vlither Android is discontinued or the service shuts down in the future, we are not responsible for the continued availability of tags or for previously paid tag fees.");
    igSpacing();
    igTextWrapped("Questions? Contact Lucky, a Staff member, or another authorized team member before submitting or purchasing a tag.");
#ifdef ANDROID
    igSpacing();
    if (igButton("Discord Support", (ImVec2){homepage_px(180.0f), 0}))
      android_jni_open_url("https://discord.gg/CJEeSScTJs");
#endif
  } else {
    igTextColored((ImVec4){0.45f, 0.75f, 1.0f, 1.0f},
                  "About Vlither Android");
    igSpacing();
    igTextWrapped(
        "Vlither Android is a highly optimized Android port of Vlither, which was originally designed for PC. Lucky ported Vlither to Android and added mobile-specific controls, interface improvements, and additional features.");
    igSpacing();
    igTextWrapped(
        "The Android build is designed to deliver smooth gameplay and target the highest stable frame rate supported by your device and its current performance limits.");
    igSpacing();
    igTextWrapped(
        "Vlither is an open-source mod. The Android project builds on that foundation while adapting the experience for phones, tablets, touch controls, and optional physical mouse and keyboard input.");
  }
  igPopTextWrapPos();
  igEndChild();

  igSeparator();
  if (igButton("Close", (ImVec2){-1, 0})) igCloseCurrentPopup();
  igPopFont();
  igEndPopup();
}

#ifdef ANDROID
#define SUPPORT_PAYPAL_HANDLE "@Vlitherandroid"
#define SUPPORT_PAYPAL_URL    "https://www.paypal.me/Vlitherandroid"
#define SUPPORT_UPI_ID        "luckyy623@ibl"
#define SUPPORT_UPI_URL       "upi://pay?pa=luckyy623@ibl&pn=Vlither%20Android&cu=INR"

static int    g_support_copied    = 0;   /* 0 none, 1 PayPal, 2 UPI */
static double g_support_copied_at = 0.0;

static void support_row(const char *title, const char *note, const char *value,
                        int idx, const char *open_label, const char *open_url) {
  char label[64];
  igTextColored((ImVec4){0.45f, 0.75f, 1.0f, 1.0f}, "%s", title);
  if (note && note[0])
    igTextColored((ImVec4){0.62f, 0.62f, 0.68f, 1.0f}, "%s", note);
  igSpacing();
  igText("%s", value);
  igSpacing();

  ImVec2 avail;
  igGetContentRegionAvail(&avail);
  ImGuiStyle *style = igGetStyle();
  float half = (avail.x - style->ItemSpacing.x) * 0.5f;

  bool just_copied = g_support_copied == idx &&
                     (igGetTime() - g_support_copied_at) < 2.0;
  snprintf(label, sizeof label, "%s##support_copy_%d",
           just_copied ? "Copied!" : "Copy", idx);
  if (igButton(label, (ImVec2){half, 0})) {
    android_jni_set_clipboard_text(value);
    g_support_copied    = idx;
    g_support_copied_at = igGetTime();
  }
  igSameLine(0.0f, -1.0f);
  snprintf(label, sizeof label, "%s##support_open_%d", open_label, idx);
  if (igButton(label, (ImVec2){half, 0})) android_jni_open_url(open_url);
}

static void draw_support_popup(tenv *env) {
  if (!env || !env->usr) return;
  tuser_data *usr = env->usr;
  ImGuiViewport *vp = igGetMainViewport();

  float popup_w = vp->WorkSize.x * 0.72f;
  if (popup_w < homepage_px(330.0f))
    popup_w = vp->WorkSize.x - homepage_px(20.0f);
  if (popup_w > homepage_px(640.0f)) popup_w = homepage_px(640.0f);
  igSetNextWindowSize((ImVec2){popup_w, 0.0f}, ImGuiCond_Appearing);

  if (!igBeginPopupModal("Support Vlither", NULL,
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_AlwaysAutoResize))
    return;

  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]->LegacySize);

  igPushTextWrapPos(0.0f);
  igText("Made by Lucky. Vlither Android is free - if you enjoy it, you can "
         "support its development. Totally optional, and thank you!");
  igPopTextWrapPos();
  igSeparator();

  support_row("PayPal", "Worldwide", SUPPORT_PAYPAL_HANDLE, 1,
              "Open PayPal", SUPPORT_PAYPAL_URL);
  igSeparator();
  support_row("UPI", "India only", SUPPORT_UPI_ID, 2,
              "Open UPI app", SUPPORT_UPI_URL);
  igSeparator();

  if (igButton("Close", (ImVec2){-1, 0})) igCloseCurrentPopup();
  igPopFont();
  igEndPopup();
}
#endif

void ui_title_screen_init(tenv* env) {}

#ifdef ANDROID
static void draw_homepage_orientation_button(tenv* env, float frame_height) {
  static const char* const names[2] = {"Landscape", "Portrait"};
  static int mode = 0;
  static bool mode_known = false;
  if (!mode_known) {
    int saved = android_jni_get_screen_orientation_mode();
    mode = saved == 2 ? 1 : 0;
    mode_known = true;
  }

    const float button_w = hp_toggle_w();
  char label[64];
  snprintf(label, sizeof label, "Screen: %s##homepage_orientation", names[mode]);
  igSetCursorPos(hp_toggle_cursor(2, button_w));
  if (igButton(label, (ImVec2){button_w, 64.0f * g_hp_u})) {
    int next = mode ? 0 : 1;
    if (android_jni_set_screen_orientation_mode(next ? 2 : 1)) mode = next;
  }
}
#endif

static void draw_server_list_popup(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;

  if (!gdata->server_list.fetching && !gdata->server_list.fetched &&
      !gdata->server_list.fetch_error)
    server_list_fetch(env);
  server_list_poll(env);

  if (gdata->server_list.fetched && gdata->server_list.count > 0 &&
      !gdata->server_list.pinging && gdata->server_list.pings_done == 0) {
    server_list_start_ping(env);
  }

  g_sl_popup_open = igBeginPopup("##sl_popup", 0);
  if (g_sl_popup_open) {
    igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
               usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

    if (gdata->server_list.fetching) {
      igTextColored((ImVec4){0.8f, 0.8f, 0.3f, 1.0f},
                    "Fetching official server list...");
    } else if (gdata->server_list.fetch_error) {
      igTextColored((ImVec4){0.9f, 0.4f, 0.4f, 1.0f},
                    "Couldn't fetch official list \xe2\x80\x94 custom servers still available.");
    } else if (gdata->server_list.fetched && gdata->server_list.count > 0) {
      if (gdata->server_list.pinging) {
        char prog[56];
        snprintf(prog, sizeof(prog), "Pinging... %d/%d",
                 gdata->server_list.pings_done, gdata->server_list.count);
        igTextColored((ImVec4){0.8f, 0.8f, 0.3f, 1.0f}, prog);
      } else {
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "%d servers (best ping first)",
                 gdata->server_list.count);
        igTextColored((ImVec4){0.5f, 0.9f, 0.5f, 1.0f}, hdr);
      }
    }

    igSeparator();

    if (gdata->server_list.count > 0) {
      float server_list_w = homepage_min(homepage_px(440.0f),
                                         ctx->size[0] - homepage_px(32.0f));
      float server_list_h = homepage_min(homepage_px(320.0f),
                                         ctx->size[1] - homepage_px(80.0f));
      igBeginChild_Str("##sl_scroll", (ImVec2){server_list_w, server_list_h},
                       ImGuiChildFlags_None, 0);

      bool sorted = !gdata->server_list.pinging &&
                    gdata->server_list.pings_done > 0;

      for (int j = 0; j < gdata->server_list.count; j++) {
        int i         = sorted ? gdata->server_list.sorted_order[j] : j;
        int ping      = gdata->server_list.pings[i];
        bool is_custom = i < gdata->server_list.custom_count;

        char name_buf[96];
        if (is_custom) {
          snprintf(name_buf, sizeof(name_buf), "\xe2\x98\x85 %s [IPv4]",
                   gdata->server_list.names[i]);
        } else if (gdata->server_list.sids[i] > 0 &&
                   gdata->server_list.sids[i] <= 9999) {
          snprintf(name_buf, sizeof(name_buf), "SID %04u  %s",
                   (unsigned)gdata->server_list.sids[i],
                   gdata->server_list.ips[i]);
        } else {
          snprintf(name_buf, sizeof(name_buf), "[IPv4] %s",
                   gdata->server_list.ips[i]);
        }

        char label[144];
        if (ping < 0) {
          snprintf(label, sizeof(label), "%-26s  --", name_buf);
        } else if (ping >= 9999) {
          snprintf(label, sizeof(label), "%-26s  !!ms", name_buf);
        } else {
          snprintf(label, sizeof(label), "%-26s  %dms", name_buf, ping);
        }

        bool pushed_color = false;
        if (is_custom) {
          igPushStyleColor_Vec4(ImGuiCol_Text,
            (ImVec4){1.0f, 0.82f, 0.25f, 1.0f});
          pushed_color = true;
        } else if (ping >= 0 && ping < 9999) {
          ImVec4 col;
          if      (ping <  80) col = (ImVec4){0.3f, 1.0f, 0.4f, 1.0f};
          else if (ping < 150) col = (ImVec4){1.0f, 1.0f, 0.3f, 1.0f};
          else if (ping < 300) col = (ImVec4){1.0f, 0.65f, 0.2f, 1.0f};
          else                  col = (ImVec4){1.0f, 0.4f, 0.4f, 1.0f};
          igPushStyleColor_Vec4(ImGuiCol_Text, col);
          pushed_color = true;
        }

        if (igSelectable_Bool(label, false,
                              ImGuiSelectableFlags_None, (ImVec2){0, 0})) {
          strncpy(usrs->server_address, gdata->server_list.ips[i],
                  MAX_SERVER_IP_LEN);
          usrs->server_address[MAX_SERVER_IP_LEN] = '\0';
          igCloseCurrentPopup();
        }
        if (is_custom && igIsItemHovered(0)) {
          igSetTooltip("%s", gdata->server_list.ips[i]);
        }

        if (pushed_color) igPopStyleColor(1);
      }
      igEndChild();
      igSeparator();
    }

    if (!gdata->server_list.fetching) {
      if (igButton("Refresh##sl_refresh", (ImVec2){0, 0}))
        server_list_fetch(env);
    }

    igPopFont();
    igEndPopup();
  }

}

/* ======================================================================
 *  Glass / gold homepage (matches the Fuji Sunset design screenshot)
 *  All sizes are in "design units": 1 unit == 1px of a 1157px-tall
 *  screenshot, scaled by g_hp_u so it fits any screen height.
 *  Tweak the numbers in ui_title_screen() to nudge the layout.
 * ====================================================================== */

static ImDrawList* g_dl;
static ImVec2 g_org;       /* window origin on screen */
static ImFont* g_font;     /* regular */
static ImFont* g_font_b;   /* bold    */

#define HP_GOLD    g_acc.x, g_acc.y, g_acc.z   /* accent, changes per background */
#define HP_DARK    0.04f, 0.04f, 0.045f

static ImU32 hp_col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}
static ImVec2 hp_sp(float x, float y) { /* design -> screen */
  return (ImVec2){g_org.x + g_ox + x * g_hp_u, g_org.y + g_oy + y * g_hp_u};
}
static ImVec2 hp_cp(float x, float y) { /* design -> cursor pos */
  return (ImVec2){g_ox + x * g_hp_u, g_oy + y * g_hp_u};
}

/* ---- text helpers (any pixel size) ---- */
static ImVec2 hp_text_size(ImFont* f, float px, const char* s) {
  ImVec2 sz;
  igPushFont(f, px);
  igCalcTextSize(&sz, s, NULL, false, -1.0f);
  igPopFont();
  return sz;
}
static void hp_text(ImFont* f, float px, ImVec2 p, ImU32 col, const char* s) {
  igPushFont(f, px);
  ImDrawList_AddText_Vec2(g_dl, p, col, s, NULL);
  igPopFont();
}
static void hp_text_c(ImFont* f, float px, ImVec2 c, ImU32 col, const char* s) {
  ImVec2 sz = hp_text_size(f, px, s);
  hp_text(f, px, (ImVec2){c.x - sz.x * 0.5f, c.y - sz.y * 0.5f}, col, s);
}

/* ---- panels ---- */
static void hp_glass(ImVec2 a, ImVec2 b, float round, float alpha) {
  ImDrawList_AddRectFilled(g_dl, a, b, hp_col(0.07f + g_acc.x * 0.05f, 0.07f + g_acc.y * 0.05f, 0.07f + g_acc.z * 0.05f, alpha), round, 0);
  ImDrawList_AddRectFilled(g_dl, a, (ImVec2){b.x, a.y + (b.y - a.y) * 0.40f},
                           hp_col(0.6f + g_acc.x * 0.4f, 0.6f + g_acc.y * 0.4f, 0.6f + g_acc.z * 0.4f, 0.05f), round,
                           ImDrawFlags_RoundCornersTop);
  ImDrawList_AddRect(g_dl, a, b, hp_col(g_acc.x * 0.92f + 0.04f, g_acc.y * 0.92f + 0.04f, g_acc.z * 0.92f + 0.04f, 0.38f), round, 0,
                     2.0f * g_hp_u);
}

/* dark rounded button; returns clicked, gives screen rect */
static bool hp_cell(const char* id, float x, float y, float w, float h,
                    float round, ImVec2* out_a, ImVec2* out_b) {
  igSetCursorPos(hp_cp(x, y));
  bool clicked = igInvisibleButton(id, (ImVec2){w * g_hp_u, h * g_hp_u}, 0);
  bool hov = igIsItemHovered(0), act = igIsItemActive();
  ImVec2 a, b;
  igGetItemRectMin(&a);
  igGetItemRectMax(&b);
  float k = act ? 0.16f : (hov ? 0.07f : 0.0f);
  ImDrawList_AddRectFilled(g_dl, a, b,
      hp_col(0.035f + k, 0.035f + k * 0.8f, 0.04f + k * 0.5f, 0.84f),
      round * g_hp_u, 0);
  ImDrawList_AddRect(g_dl, a, b, hp_col(1, 1, 1, act ? 0.30f : 0.10f),
                     round * g_hp_u, 0, 1.5f * g_hp_u);
  if (out_a) *out_a = a;
  if (out_b) *out_b = b;
  return clicked;
}

/* ---- icons: font glyphs ---- */
static void hp_glyph(ImVec2 c, float px, ImU32 col, const char* g) {
  hp_text_c(g_font, px, c, col, g);
}

/* ---- icons: vector (not in iconfont) ---- */
static void hp_icon_palette(ImVec2 c, float r) {
  ImU32 gold = hp_col(HP_GOLD, 1), dark = hp_col(HP_DARK, 1);
  ImDrawList_AddCircleFilled(g_dl, c, r, gold, 40);
  static const float dx[4] = {-0.45f, -0.12f, 0.28f, 0.55f};
  static const float dy[4] = {-0.05f, -0.50f, -0.46f, -0.02f};
  for (int i = 0; i < 4; i++)
    ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x + dx[i] * r, c.y + dy[i] * r},
                               r * 0.14f, dark, 12);
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x + 0.30f * r, c.y + 0.50f * r},
                             r * 0.30f, dark, 16);
}

static void hp_icon_podium(ImVec2 c, float s) {
  ImU32 gold = hp_col(HP_GOLD, 1), dark = hp_col(HP_DARK, 1);
  float w = s * 0.40f, base = c.y + s * 0.55f;
  float hh[3] = {s * 0.60f, s * 1.00f, s * 0.42f};
  const char* num[3] = {"2", "1", "3"};
  for (int i = 0; i < 3; i++) {
    float x0 = c.x + (i - 1.5f) * w + w * 0.0f;
    ImVec2 a = {x0, base - hh[i]}, b = {x0 + w * 0.92f, base};
    ImDrawList_AddRectFilled(g_dl, a, b, gold, s * 0.06f, 0);
    hp_text_c(g_font_b, s * 0.34f, (ImVec2){(a.x + b.x) * 0.5f, base - hh[i] * 0.45f},
              dark, num[i]);
  }
  /* little star on the winner's block */
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x - w * 0.5f + w * 0.46f, base - hh[1] - s * 0.14f},
                             s * 0.11f, gold, 12);
}

static void hp_icon_gamepad(ImVec2 c, float s) {
  ImU32 body = hp_col(HP_GOLD, 1), dark = hp_col(HP_DARK, 1);
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x - 0.58f * s, c.y + 0.22f * s}, 0.34f * s, body, 20);
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x + 0.58f * s, c.y + 0.22f * s}, 0.34f * s, body, 20);
  ImDrawList_AddRectFilled(g_dl, (ImVec2){c.x - 0.74f * s, c.y - 0.40f * s},
                           (ImVec2){c.x + 0.74f * s, c.y + 0.30f * s}, body, 0.30f * s, 0);
  /* d-pad */
  ImVec2 d = {c.x - 0.40f * s, c.y - 0.04f * s};
  ImDrawList_AddRectFilled(g_dl, (ImVec2){d.x - 0.20f * s, d.y - 0.06f * s},
                           (ImVec2){d.x + 0.20f * s, d.y + 0.06f * s}, dark, 0, 0);
  ImDrawList_AddRectFilled(g_dl, (ImVec2){d.x - 0.06f * s, d.y - 0.20f * s},
                           (ImVec2){d.x + 0.06f * s, d.y + 0.20f * s}, dark, 0, 0);
  /* buttons */
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x + 0.34f * s, c.y - 0.16f * s}, 0.09f * s, dark, 10);
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x + 0.56f * s, c.y + 0.02f * s}, 0.09f * s, dark, 10);
}

static void hp_icon_skull(ImVec2 c, float s, ImU32 col, ImU32 dark) {
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x, c.y - 0.10f * s}, 0.50f * s, col, 28);
  ImDrawList_AddRectFilled(g_dl, (ImVec2){c.x - 0.28f * s, c.y + 0.20f * s},
                           (ImVec2){c.x + 0.28f * s, c.y + 0.56f * s}, col, 0.08f * s, 0);
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x - 0.20f * s, c.y - 0.06f * s}, 0.14f * s, dark, 14);
  ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x + 0.20f * s, c.y - 0.06f * s}, 0.14f * s, dark, 14);
  ImDrawList_AddTriangleFilled(g_dl, (ImVec2){c.x, c.y + 0.06f * s},
                               (ImVec2){c.x - 0.06f * s, c.y + 0.18f * s},
                               (ImVec2){c.x + 0.06f * s, c.y + 0.18f * s}, dark);
  for (int i = -1; i <= 1; i += 2)
    ImDrawList_AddLine(g_dl, (ImVec2){c.x + i * 0.09f * s, c.y + 0.34f * s},
                       (ImVec2){c.x + i * 0.09f * s, c.y + 0.56f * s}, dark, 0.05f * s);
}

static void hp_icon_swords(ImVec2 c, float s, ImU32 col) {
  float t = 0.12f * s, k = 0.7071f;
  for (int i = -1; i <= 1; i += 2) {
    ImVec2 tip = {c.x - i * 0.50f * s, c.y - 0.50f * s};
    ImVec2 hilt = {c.x + i * 0.50f * s, c.y + 0.50f * s};
    ImDrawList_AddLine(g_dl, tip, hilt, col, t);
    ImVec2 d = {i * k, k};            /* tip -> hilt */
    ImVec2 p = {-k, i * k};           /* perpendicular */
    ImVec2 g = {hilt.x - d.x * 0.26f * s, hilt.y - d.y * 0.26f * s};
    ImDrawList_AddLine(g_dl, (ImVec2){g.x - p.x * 0.24f * s, g.y - p.y * 0.24f * s},
                       (ImVec2){g.x + p.x * 0.24f * s, g.y + p.y * 0.24f * s}, col, t);
    ImDrawList_AddCircleFilled(g_dl, hilt, 0.09f * s, col, 10);
  }
}

static void hp_icon_user_outline(ImVec2 c, float s, ImU32 col) {
  float t = 0.085f * s;
  ImDrawList_AddCircle(g_dl, (ImVec2){c.x, c.y - 0.24f * s}, 0.30f * s, col, 28, t);
  ImDrawList_PathClear(g_dl);
  ImDrawList_PathArcTo(g_dl, (ImVec2){c.x, c.y + 0.80f * s}, 0.66f * s,
                       3.14159f * 1.12f, 3.14159f * 1.88f, 24);
  ImDrawList_PathStroke(g_dl, col, 0, t);
}

/* label centred inside a rect (optional glyph/icon drawn by caller) */
static void hp_label_in(ImVec2 a, ImVec2 b, float fy, float px, const char* s) {
  hp_text_c(g_font, px, (ImVec2){(a.x + b.x) * 0.5f, a.y + (b.y - a.y) * fy},
            hp_col(1, 1, 1, 0.96f), s);
}

/* icon + text on one line, centred */
static void hp_inline(ImVec2 a, ImVec2 b, const char* glyph, float gpx, ImU32 gcol,
                      const char* s, float px) {
  float gw = glyph ? gpx * 1.1f : 0.0f;
  float gap = glyph ? 14.0f * g_hp_u : 0.0f;
  ImVec2 tsz = hp_text_size(g_font, px, s);
  float total = gw + gap + tsz.x;
  float cy = (a.y + b.y) * 0.5f;
  float x = (a.x + b.x) * 0.5f - total * 0.5f;
  if (glyph) hp_glyph((ImVec2){x + gw * 0.5f, cy}, gpx, gcol, glyph);
  hp_text(g_font, px, (ImVec2){x + gw + gap, cy - tsz.y * 0.5f},
          hp_col(1, 1, 1, 0.96f), s);
}

/* the gold PLAY button */
static bool hp_play_button(float x, float y, float w, float h) {
  igSetCursorPos(hp_cp(x, y));
  bool clicked = igInvisibleButton("##hp_play", (ImVec2){w * g_hp_u, h * g_hp_u}, 0);
  bool hov = igIsItemHovered(0), act = igIsItemActive();
  ImVec2 a, b;
  igGetItemRectMin(&a);
  igGetItemRectMax(&b);
  float r = 20.0f * g_hp_u, k = act ? -0.10f : (hov ? 0.06f : 0.0f);
  ImDrawList_AddRectFilled(g_dl, a, b, hp_col(g_acc.x * 0.74f + k, g_acc.y * 0.74f + k, g_acc.z * 0.74f + k, 0.96f), r, 0);
  ImDrawList_AddRectFilled(g_dl, a, (ImVec2){b.x, a.y + (b.y - a.y) * 0.50f},
                           hp_col(0.5f + g_acc.x * 0.5f, 0.5f + g_acc.y * 0.5f, 0.5f + g_acc.z * 0.5f, 0.30f), r, ImDrawFlags_RoundCornersTop);
  ImDrawList_AddRect(g_dl, a, b, hp_col(0.35f + g_acc.x * 0.65f, 0.35f + g_acc.y * 0.65f, 0.35f + g_acc.z * 0.65f, 0.95f), r, 0, 3.0f * g_hp_u);

  float px = 64.0f * g_hp_u, tri = 38.0f * g_hp_u, gap = 18.0f * g_hp_u;
  ImVec2 tsz = hp_text_size(g_font_b, px, "PLAY");
  float total = tri + gap + tsz.x;
  float cx = (a.x + b.x) * 0.5f - total * 0.5f, cy = (a.y + b.y) * 0.5f;
  ImDrawList_AddTriangleFilled(g_dl, (ImVec2){cx, cy - tri * 0.5f},
                               (ImVec2){cx, cy + tri * 0.5f},
                               (ImVec2){cx + tri * 0.9f, cy}, hp_col(1, 1, 1, 0.97f));
  hp_text(g_font_b, px, (ImVec2){cx + tri + gap, cy - tsz.y * 0.5f}, hp_col(1, 1, 1, 0.97f), "PLAY");
  return clicked;
}

/* small pill in the bottom bar, auto-sized, wraps to a new row if needed */
static float g_bar_x0 = 48.0f, g_bar_xmax = 1e9f, g_pill_x, g_pill_y;
static bool hp_pill(const char* id, const char* label, float* x, float* y, float min_w) {
  float px = 32.0f * g_hp_u;
  ImVec2 tsz = hp_text_size(g_font, px, label);
  float w = fmaxf(min_w, tsz.x / g_hp_u + 44.0f);
  if (*x + w > g_bar_xmax) { *x = g_bar_x0; *y += 66.0f; }
  g_pill_x = *x;
  g_pill_y = *y;
  ImVec2 a, b;
  bool c = hp_cell(id, *x, *y, w, 52.0f, 14.0f, &a, &b);
  hp_label_in(a, b, 0.5f, px, label);
  *x += w + 15.0f;
  return c;
}

static void hp_play(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  const char* entered = usrs->server_address;
  bool sid_only = strlen(entered) == 4;
  for (int i = 0; sid_only && i < 4; ++i)
    if (entered[i] < '0' || entered[i] > '9') sid_only = false;
  if (sid_only) {
    int sid = atoi(entered);
    if (!server_list_resolve_sid(env, sid, usrs->server_address,
                                 sizeof(usrs->server_address)))
      return;
  }
  usr->gdata.conn = CONNECTING;
  usr->gdata.curr_screen = PLAYING;
  glfwSetTime(0);
  server_connect(env);
}

void ui_title_screen(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  game_data* gdata = &usr->gdata;

  apply_homepage_background(env);

  const float W = (float)ctx->size[0], H = (float)ctx->size[1];
  const bool portrait = W < H;
  g_hp_portrait = portrait;
  g_hp_w = W;
  g_acc = homepage_accent(usrs->homepage_background);   /* accent follows background */

  /* Scale for toggles / stats / bottom bar:
   *   landscape: 1 unit = H/1157   (design screenshot height)
   *   portrait : 1 unit = W/1000   (940-wide column with 30 margins)   */
  float uT = portrait ? W / 1000.0f : H / 1157.0f;
  if (uT < 0.35f) uT = 0.35f;
  const float WdT = W / uT, HdT = H / uT;
  g_ox = g_oy = 0.0f;
  g_hp_u = uT;
  float u = uT;

  g_font = usr->imgui_data.regular_font[usrs->ui_font_size];
  g_font_b = usr->imgui_data.regular_font_bold[usrs->ui_font_size];
  g_dl = igGetWindowDrawList();
  igGetWindowPos(&g_org);

  /* version, top-right corner */
  {
    char version_str[16] = {0};
    sprintf(version_str, "v%s", APP_VERSION);
    igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
               usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
    ImVec2 vtxtsz;
    igCalcTextSize(&vtxtsz, version_str, NULL, false, -1);
    igSetCursorPos((ImVec2){W - vtxtsz.x - 8.0f * u, 4.0f * u});
    igTextColored((ImVec4){0.4f + g_acc.x * 0.6f, 0.4f + g_acc.y * 0.6f,
                           0.4f + g_acc.z * 0.6f, 0.85f}, "%s", version_str);
    igPopFont();
  }

  igPushFont(g_font, g_font->LegacySize);
  push_homepage_theme(usrs->homepage_background);
  const float frame_height = igGetFrameHeight();
  const float fs = igGetFontSize();

  /* ================= toggles ================= */
  if (portrait) {
    g_tog_w_p = 940.0f * u;
    for (int i = 0; i < 3; i++) {
      g_tog_x[i] = 30.0f * u;
      g_tog_y[i] = (255.0f + i * 78.0f) * u;
    }
  }
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 32.0f * u);
  igPushStyleVar_Float(ImGuiStyleVar_FrameBorderSize, 1.0f);
  igPushStyleVar_Vec2(ImGuiStyleVar_ButtonTextAlign, (ImVec2){0.0f, 0.5f});
  igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){28.0f * u, style->FramePadding.y});
  draw_homepage_background_picker(env, frame_height);
  draw_snakey_rain_homepage(env, frame_height);
#ifdef ANDROID
  draw_homepage_orientation_button(env, frame_height);
#endif
  igPopStyleVar(4);

  /* ================= panel placement ================= */
  const float px0 = 60.0f, py0 = 68.0f, pw = 628.0f;
  float ph;
  float bar_y0 = portrait ? HdT - 128.0f : HdT - 62.0f;   /* design units (uT) */
  if (portrait) {
    ph = 1020.0f;
    float top_px = 500.0f * uT;
    float bot_px = (bar_y0 - 24.0f) * uT;
    float ps = (bot_px - top_px) / (ph * uT);
    if (ps > 1.3f) ps = 1.3f;
    if (ps > 940.0f / 628.0f) ps = 940.0f / 628.0f;
    if (ps < 0.5f) ps = 0.5f;
    u = uT * ps;
    g_hp_u = u;
    float left = (W - pw * u) * 0.5f;
    float top = top_px + (bot_px - top_px - ph * u) * 0.5f;
    g_ox = left - px0 * u;
    g_oy = top - py0 * u;
  } else {
    ph = HdT - 68.0f - 69.0f;
  }

  /* ================= left glass panel ================= */
  hp_glass(hp_sp(px0, py0), hp_sp(px0 + pw, py0 + ph), 30.0f * u, 0.72f);

  /* -- header: avatar + @name (tap to edit nickname) -- */
  {
    ImVec2 a, b;
    /* The avatar button below overlaps this header cell. ImGui gives hover to
     * the FIRST overlapping item unless it allows overlap, so without this the
     * header swallowed every avatar tap and the photo picker never opened. */
    igSetNextItemAllowOverlap();
    bool clicked = hp_cell("##hp_header", 88, 97, 572, 128, 18, &a, &b);
    ImVec2 av_b = hp_sp(88 + 128, 97 + 128);
    ImVec2 av_a = hp_sp(88, 97);
    ImDrawList_AddRectFilled(g_dl, av_a, av_b, hp_col(0.10f, 0.10f, 0.12f, 0.95f), 16 * u, 0);

    bool avatar_clicked = false;
    bool have_photo = false;
#ifdef ANDROID
    /* profile photo: poll for a newly picked image a couple of times a second */
    {
      static int poll = 0;
      if (usr->r && ctx && (poll++ % 30) == 0)
        renderer_reload_profile_image(usr->r, ctx);
      have_photo = usr->r && usr->r->profile_ds;
    }
#endif
    if (have_photo) {
      ImTextureRef ptex = {NULL, (ImTextureID)usr->r->profile_ds};
      ImDrawList_AddImageRounded(g_dl, ptex, av_a, av_b, (ImVec2){0, 0},
                                 (ImVec2){1, 1}, hp_col(1, 1, 1, 1), 16 * u, 0);
    } else {
      hp_icon_user_outline((ImVec2){(av_a.x + av_b.x) * 0.5f, (av_a.y + av_b.y) * 0.5f - 6 * u},
                           72.0f * u, hp_col(1, 1, 1, 0.92f));
    }
    ImDrawList_AddRect(g_dl, av_a, av_b, hp_col(1, 1, 1, 0.22f), 16 * u, 0, 1.5f * u);

    /* tapping the avatar opens local storage to pick / change the photo */
    igSetCursorPos(hp_cp(88, 97));
    avatar_clicked = igInvisibleButton("##hp_avatar", (ImVec2){128 * u, 128 * u}, 0);
#ifdef ANDROID
    if (avatar_clicked) android_jni_request_profile_image();
#endif

    char name[64];
    if (usrs->nickname[0]) snprintf(name, sizeof name, "%s", usrs->nickname);
    else snprintf(name, sizeof name, "Tap to set name");
    hp_text(g_font_b, 56.0f * u, hp_sp(240, 97 + 64 - 30), hp_col(1, 1, 1, 0.97f), name);
    if (clicked && !avatar_clicked) igOpenPopup_Str("##nick_popup", 0);
  }
  if (igBeginPopup("##nick_popup", ImGuiWindowFlags_NoSavedSettings)) {
    igText("Nickname");
    igSetNextItemWidth(320.0f * u);
    igInputTextWithHint("##nickname_input", "Nickname", usrs->nickname,
                        MAX_NICKNAME_LEN + 1, ImGuiInputTextFlags_None, NULL, NULL);
    if (igIsItemDeactivatedAfterEdit()) save_user_settings(usrs);
    if (igButton("Done", (ImVec2){-1.0f, 0.0f})) {
      save_user_settings(usrs);
      igCloseCurrentPopup();
    }
    igEndPopup();
  }

  /* -- server field + globe -- */
  {
    float pad_y = fmaxf(2.0f, (74.0f * u - fs) * 0.5f);
    igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){20.0f * u, pad_y});
    igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 14.0f * u);
    igPushStyleVar_Float(ImGuiStyleVar_FrameBorderSize, 1.0f);
    igSetCursorPos(hp_cp(88.0f, 258.0f));
    igSetNextItemWidth(478.0f * u);
    igInputTextWithHint("##ipv4_input", "IPv4:Port or 4-digit SID",
                        usrs->server_address, MAX_SERVER_IP_LEN + 1,
                        ImGuiInputTextFlags_None, NULL, NULL);
    igPopStyleVar(3);

    ImVec2 a, b;
    if (hp_cell("##hp_globe", 587, 258, 72, 74, 14, &a, &b)) {
      if (!gdata->server_list.fetching && !gdata->server_list.fetched)
        server_list_fetch(env);
      igOpenPopup_Str("##sl_popup", 0);
    }
    hp_glyph((ImVec2){(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, 40.0f * u,
             hp_col(0.3f + g_acc.x * 0.7f, 0.3f + g_acc.y * 0.7f, 0.3f + g_acc.z * 0.7f, 1),
             "\ue9c9");
  }
  draw_server_list_popup(env);

  /* -- PLAY -- */
  if (hp_play_button(88, 363, 572, 100)) hp_play(env);

  /* -- button grid -- */
  {
    ImVec2 a, b, c;
    const ImU32 gold = hp_col(HP_GOLD, 1);
    const float lab = 33.0f * u;

    if (hp_cell("##hp_skin", 88, 500, 276, 150, 18, &a, &b))
      usr->gdata.curr_screen = SKIN_EDITOR;
    hp_icon_palette((ImVec2){(a.x + b.x) * 0.5f, a.y + (b.y - a.y) * 0.36f}, 31.0f * u);
    hp_label_in(a, b, 0.78f, lab, "Skin Editor");

    if (hp_cell("##hp_lb", 383, 500, 276, 150, 18, &a, &b)) {
      ui_leaderboard_panel_open();
      usr->gdata.curr_screen = LEADERBOARD_PANEL;
    }
    hp_icon_podium((ImVec2){(a.x + b.x) * 0.5f, a.y + (b.y - a.y) * 0.36f}, 62.0f * u);
    hp_label_in(a, b, 0.78f, lab, "Leaderboard");

    if (hp_cell("##hp_controls", 88, 668, 276, 102, 18, &a, &b))
      usr->gdata.curr_screen = CONTROLS;
    {
      float cy = (a.y + b.y) * 0.5f;
      ImVec2 tsz = hp_text_size(g_font, lab, "Controls");
      float total = 60.0f * u + 14.0f * u + tsz.x;
      float x = (a.x + b.x) * 0.5f - total * 0.5f;
      hp_icon_gamepad((ImVec2){x + 30.0f * u, cy}, 54.0f * u);
      hp_text(g_font, lab, (ImVec2){x + 74.0f * u, cy - tsz.y * 0.5f}, hp_col(1, 1, 1, 0.96f), "Controls");
    }

    if (hp_cell("##hp_chat", 383, 668, 276, 102, 18, &a, &b))
      usr->gdata.curr_screen = NTL_PANEL;
    {
      float cy = (a.y + b.y) * 0.5f;
      ImVec2 tsz = hp_text_size(g_font, lab, "Chat");
      float total = 56.0f * u + 14.0f * u + tsz.x;
      float x = (a.x + b.x) * 0.5f - total * 0.5f;
      c = (ImVec2){x + 28.0f * u, cy};
      hp_glyph(c, 56.0f * u, gold, "\ue96b");
      for (int i = -1; i <= 1; i++)
        ImDrawList_AddCircleFilled(g_dl, (ImVec2){c.x + i * 10.0f * u, c.y - 2.0f * u},
                                   3.4f * u, hp_col(HP_DARK, 1), 8);
      hp_text(g_font, lab, (ImVec2){x + 70.0f * u, cy - tsz.y * 0.5f}, hp_col(1, 1, 1, 0.96f), "Chat");
    }

    if (hp_cell("##hp_tags", 88, 790, 179, 162, 18, &a, &b))
      android_jni_open_webview("https://vlitherandroid.onrender.com/store");
    hp_glyph((ImVec2){(a.x + b.x) * 0.5f, a.y + (b.y - a.y) * 0.31f}, 66.0f * u, gold, "\ue935");
    hp_label_in(a, b, 0.76f, lab, "Tags Store");

    if (hp_cell("##hp_sound", 287, 790, 193, 162, 18, &a, &b))
      android_jni_open_audio_settings();
    hp_glyph((ImVec2){(a.x + b.x) * 0.5f, a.y + (b.y - a.y) * 0.29f}, 66.0f * u, gold, "\ue911");
    hp_label_in(a, b, 0.62f, lab, "Sound/Song");
    hp_label_in(a, b, 0.82f, lab, "Management");

    if (hp_cell("##hp_settings", 500, 790, 159, 162, 18, &a, &b))
      usr->gdata.curr_screen = SETTINGS;
    hp_glyph((ImVec2){(a.x + b.x) * 0.5f, a.y + (b.y - a.y) * 0.31f}, 66.0f * u, gold, "\ue994");
    hp_label_in(a, b, 0.76f, lab, "Settings");

    if (hp_cell("##hp_quit", 88, 990, 572, 70, 16, &a, &b)) {
      env->config.running = false;
      save_user_settings(usrs);
    }
    hp_inline(a, b, "\ue9b6", 40.0f * u, gold, "Quit", lab);
  }

  /* back to the top/bottom scale, no offset */
  g_ox = g_oy = 0.0f;
  g_hp_u = uT;
  u = uT;

  /* ================= stats card ================= */
  {
    float cw = portrait ? 940.0f : 702.0f, ch = 205.0f;
    float cx = portrait ? 30.0f : 955.0f, cy = portrait ? 30.0f : 68.0f;
    if (!portrait) {
      float max_x = WdT - 16.0f - 420.0f - 24.0f - cw;   /* keep clear of toggles */
      if (cx > max_x) cx = fmaxf(px0 + pw + 20.0f, max_x);
    }
    hp_glass(hp_sp(cx, cy), hp_sp(cx + cw, cy + ch), 22.0f * u, 0.74f);

    int best_score = lifetime_stats_best_length();
    int best_kills = lifetime_stats_best_match_kills();
    if (best_score < usrs->score) best_score = usrs->score;
    if (best_kills < usrs->kills) best_kills = usrs->kills;
    int tot_sec = (int)usrs->play_time;

    const float rowy[3] = {cy + 40.0f, cy + 102.0f, cy + 164.0f};
    const float c1 = cx + 56.0f, c2 = cx + cw * 0.524f;
    const float tpx = 38.0f * u, ipx = 44.0f * u;
    const ImU32 white = hp_col(1, 1, 1, 0.95f);
    const ImU32 gold = hp_col(HP_GOLD, 1);
    const ImU32 grey = hp_col(0.78f, 0.80f, 0.86f, 1);
    char buf[64];

    hp_glyph(hp_sp(c1, rowy[0]), ipx, gold, "\ue99e");
    snprintf(buf, sizeof buf, "LAST: %d", usrs->score);
    hp_text(g_font, tpx, hp_sp(c1 + 38, rowy[0] - 19), white, buf);
    hp_glyph(hp_sp(c2, rowy[0]), ipx, gold, "\ue9d9");
    snprintf(buf, sizeof buf, "BEST: %d", best_score);
    hp_text(g_font, tpx, hp_sp(c2 + 38, rowy[0] - 19), white, buf);

    hp_icon_swords(hp_sp(c1, rowy[1]), 40.0f * u, grey);
    snprintf(buf, sizeof buf, "LAST: %d", usrs->kills);
    hp_text(g_font, tpx, hp_sp(c1 + 38, rowy[1] - 19), white, buf);
    hp_glyph(hp_sp(c2, rowy[1]), ipx, hp_col(1.0f, 0.55f, 0.15f, 1), "\ue9a9");
    snprintf(buf, sizeof buf, "BEST: %d", best_kills);
    hp_text(g_font, tpx, hp_sp(c2 + 38, rowy[1] - 19), white, buf);

    hp_glyph(hp_sp(c1, rowy[2]), ipx, grey, "\ue952");
    snprintf(buf, sizeof buf, "%02d:%02d:%02d", tot_sec / 3600,
             (tot_sec % 3600) / 60, tot_sec % 60);
    hp_text(g_font, tpx, hp_sp(c1 + 38, rowy[2] - 19), white, buf);
    hp_icon_skull(hp_sp(c2, rowy[2]), 40.0f * u, hp_col(0.95f, 0.95f, 0.97f, 1),
                  hp_col(0.10f, 0.09f, 0.08f, 1));
    snprintf(buf, sizeof buf, "ALL KILLS: %d", lifetime_stats_total_kills());
    hp_text(g_font, tpx, hp_sp(c2 + 38, rowy[2] - 19), white, buf);
  }

  /* ================= bottom link bar ================= */
#ifdef ANDROID
  {
    g_bar_x0 = portrait ? 30.0f : 48.0f;
    g_bar_xmax = WdT - 30.0f;
    float by = bar_y0;
    float x = g_bar_x0;
    if (hp_pill("##hp_privacy", "Privacy & Policy", &x, &by, 229)) {
      g_privacy_section = 0;
      igOpenPopup_Str("Privacy & Policy", 0);
    }

    bool events_clicked = hp_pill("##hp_events", "Events", &x, &by, 116);
    float ex = g_pill_x, ey = g_pill_y;
    if (events_clicked) {
      vlither_event_refresh();
      usr->gdata.curr_screen = EVENTS_PANEL;
    }
    int next_event = vlither_event_next_interested();
    if (next_event >= 0) {
      long long start_ms = vlither_event_start_at_ms(next_event);
      long long now_ms = event_now_ms();
      char countdown[64];
      igSetCursorPos(hp_cp(ex, ey - 62.0f));
      igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
                 usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
      igPushTextWrapPos(ex * u + 300.0f * u);
      if (start_ms <= now_ms) {
        igTextColored((ImVec4){0.20f, 0.95f, 0.56f, 1.0f}, "LIVE: %s",
                      vlither_event_name_at(next_event));
      } else {
        event_format_countdown(start_ms - now_ms, countdown, sizeof countdown);
        igTextColored((ImVec4){0.96f, 0.78f, 0.28f, 1.0f}, "%s\nStarts in %s",
                      vlither_event_name_at(next_event), countdown);
      }
      igPopTextWrapPos();
      igPopFont();
    } else {
      int ntl_idx = vlither_ntl_event_featured(event_now_ms());
      if (ntl_idx >= 0) {
        long long start_ms = vlither_ntl_event_start_at_ms(ntl_idx);
        long long now_ms = event_now_ms();
        char countdown[64];
        igSetCursorPos(hp_cp(ex, ey - 62.0f));
        igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
                   usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
        igPushTextWrapPos(ex * u + 300.0f * u);
        if (start_ms <= now_ms) {
          igTextColored((ImVec4){0.20f, 0.95f, 0.56f, 1.0f}, "LIVE: %s",
                        vlither_ntl_event_type_at(ntl_idx));
        } else {
          event_format_countdown(start_ms - now_ms, countdown,
                                 sizeof countdown);
          igTextColored((ImVec4){0.96f, 0.78f, 0.28f, 1.0f},
                        "%s\nStarts in %s",
                        vlither_ntl_event_type_at(ntl_idx), countdown);
        }
        igPopTextWrapPos();
        igPopFont();
      }
    }

    char ratings_label[64];
    snprintf(ratings_label, sizeof ratings_label, "%s",
             vlither_ratings_has_unread_admin_reply() ? "Ratings & Reviews  \xe2\x80\xa2"
                                                      : "Ratings & Reviews");
    if (hp_pill("##hp_ratings", ratings_label, &x, &by, 262)) {
      ui_ratings_panel_open();
      usr->gdata.curr_screen = RATINGS_PANEL;
    }

    if (hp_pill("##hp_website", "Vlither Website", &x, &by, 220))
      android_jni_open_url("https://vlitherandroid.onrender.com/");

    if (hp_pill("##hp_support", "Support", &x, &by, 150))
      igOpenPopup_Str("Support Vlither", 0);

    if (usr->r && usr->r->discord_ds) {
      if (x + 64.0f > g_bar_xmax) { x = g_bar_x0; by += 66.0f; }
      ImVec2 a, b;
      if (hp_cell("##hp_discord", x, by, 64, 52, 14, &a, &b))
        android_jni_open_url("https://discord.gg/CJEeSScTJs");
      ImTextureRef tex = {NULL, (ImTextureID)usr->r->discord_ds};
      float pad = 8.0f * u;
      ImDrawList_AddImage(g_dl, tex, (ImVec2){a.x + pad, a.y + pad},
                          (ImVec2){b.x - pad, b.y - pad}, (ImVec2){0, 0},
                          (ImVec2){1, 1}, hp_col(1, 1, 1, 1));
    }
  }
#endif

  draw_privacy_policy_popup(env);
#ifdef ANDROID
  draw_support_popup(env);
#endif

  igPopStyleColor(6);
  igPopFont();
}

void ui_title_screen_destroy(tenv* env) {}
