#include "title_screen.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#include "../android_jni.h"
#endif

#include "../network/server.h"
#include "../user.h"
#include "../game/vlither_tags.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

bool g_sl_popup_open = false;

static int g_privacy_section = 0;

static long long event_now_ms(void) {
  return (long long)time(NULL) * 1000LL;
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

static void join_event(tenv *env, int index) {
  if (!env || !env->usr) return;
  const char *server_ip = vlither_event_server_at(index);
  if (!server_ip || !server_ip[0]) return;
  tuser_data *usr = env->usr;
  strncpy(usr->usrs.ipv4, server_ip, MAX_IPV4_LEN);
  usr->usrs.ipv4[MAX_IPV4_LEN] = 0;
  save_user_settings(&usr->usrs);
  usr->gdata.conn = CONNECTING;
  usr->gdata.curr_screen = PLAYING;
  glfwSetTime(0);
  server_connect(env);
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
                 (ImVec2){180.0f, 0}))
      vlither_event_set_interested(i, !interested);
    if (interested) igPopStyleColor(1);

    if (interested) {
      igSameLine(0, -1);
      if (igButton("Join Event", (ImVec2){180.0f, 0}))
        join_event(env, i);
    } else {
      igTextDisabled("Turn on Interested to unlock one-tap joining and reminders.");
    }
    igSpacing();
    igPopID();
  }
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
  if (popup_w < 330.0f) popup_w = vp->WorkSize.x - 20.0f;
  if (popup_w > 760.0f) popup_w = 760.0f;
  if (popup_h < 360.0f) popup_h = vp->WorkSize.y - 20.0f;
  if (popup_h > 680.0f) popup_h = 680.0f;
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
    if (igButton("Discord Support", (ImVec2){180, 0}))
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

void ui_title_screen_init(tenv* env) {}

void ui_title_screen(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  ImGuiIO* io = igGetIO_Nil();
  game_data* gdata = &usr->gdata;

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

#ifdef ANDROID
  /* Homepage policy entry point and the supplied Discord artwork. The image is
     intentionally used as the Discord link instead of a second text button. */
  igSetCursorPos((ImVec2){16.0f, 16.0f});
  if (igButton("Privacy & Policy", (ImVec2){170.0f, frame_height * 1.15f})) {
    g_privacy_section = 0;
    igOpenPopup_Str("Privacy & Policy", 0);
  }

  if (usr->r && usr->r->discord_ds) {
    float discord_size = 82.0f;
    igSetCursorPos((ImVec2){16.0f, 16.0f + frame_height * 1.15f + 8.0f});
    ImTextureRef discord_tex = {NULL, (ImTextureID)usr->r->discord_ds};
    if (igImageButton("##discord_home", discord_tex,
                      (ImVec2){discord_size, discord_size},
                      (ImVec2){0, 0}, (ImVec2){1, 1},
                      (ImVec4){0, 0, 0, 0}, (ImVec4){1, 1, 1, 1}))
      android_jni_open_url("https://discord.gg/CJEeSScTJs");
    if (igIsItemHovered(0)) igSetTooltip("Open Vlither Discord");
  }

  /* Event access stays directly below Discord as requested. The compact
     countdown is shown only for this player's interested event. */
  const float event_button_y =
      16.0f + frame_height * 1.15f + 8.0f + 82.0f + 8.0f;
  igSetCursorPos((ImVec2){16.0f, event_button_y});
  if (igButton("Events", (ImVec2){170.0f, frame_height * 1.15f})) {
    vlither_event_refresh();
    usr->gdata.curr_screen = EVENTS_PANEL;
  }
  int next_event = vlither_event_next_interested();
  if (next_event >= 0) {
    long long start_ms = vlither_event_start_at_ms(next_event);
    long long now_ms = event_now_ms();
    char countdown[64];
    igSetCursorPos((ImVec2){16.0f,
        event_button_y + frame_height * 1.15f + 5.0f});
    igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
               usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
    igPushTextWrapPos(186.0f);
    if (start_ms <= now_ms) {
      igTextColored((ImVec4){0.20f, 0.95f, 0.56f, 1.0f},
                    "LIVE: %s", vlither_event_name_at(next_event));
    } else {
      event_format_countdown(start_ms - now_ms, countdown, sizeof countdown);
      igTextColored((ImVec4){0.96f, 0.78f, 0.28f, 1.0f},
                    "%s\nStarts in %s",
                    vlither_event_name_at(next_event), countdown);
    }
    igPopTextWrapPos();
    igPopFont();
  }
#endif

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
  float sl_btn_w = frame_height;
  igPushItemWidth(logo_size - sl_btn_w - style->ItemSpacing.x);
  igInputTextWithHint("##ipv4_input", "IPv4:Port", usrs->ipv4, MAX_IPV4_LEN + 1,
                      ImGuiInputTextFlags_None, NULL, NULL);
  igPopItemWidth();
  igPopStyleColor(1);
  igPopItemWidth();

  igSameLine(0, style->ItemSpacing.x);
  igPushFont(usr->imgui_data.mono_font[usrs->ui_font_size],
             usr->imgui_data.mono_font[usrs->ui_font_size]->LegacySize);
  if (igButton("\ue9c9##sl_btn", (ImVec2){sl_btn_w, sl_btn_w})) {
    if (!gdata->server_list.fetching && !gdata->server_list.fetched)
      server_list_fetch(env);
    igOpenPopup_Str("##sl_popup", 0);
  }
  igPopFont();

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
      igBeginChild_Str("##sl_scroll", (ImVec2){340, 320},
                       ImGuiChildFlags_None, 0);

      bool sorted = !gdata->server_list.pinging &&
                    gdata->server_list.pings_done > 0;

      for (int j = 0; j < gdata->server_list.count; j++) {
        int i         = sorted ? gdata->server_list.sorted_order[j] : j;
        int ping      = gdata->server_list.pings[i];
        bool is_custom = i < gdata->server_list.custom_count;

        char name_buf[40];
        if (is_custom) {
          snprintf(name_buf, sizeof(name_buf), "\xe2\x98\x85 %s",
                    CUSTOM_SERVER_NAMES[i]);
        } else {
          snprintf(name_buf, sizeof(name_buf), "%s", gdata->server_list.ips[i]);
        }

        char label[80];
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
          strncpy(usrs->ipv4, gdata->server_list.ips[i], MAX_IPV4_LEN);
          usrs->ipv4[MAX_IPV4_LEN] = '\0';
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
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 5 +
                  frame_height * 4);
  if (igButton("\ue991 Controls",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usr->gdata.curr_screen = CONTROLS;
  }
  igSameLine(0, -1);
  if (igButton("Chat",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usr->gdata.curr_screen = NTL_PANEL;
  }

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 6 +
                  frame_height * 5);
  if (igButton("Voice Chat", (ImVec2){logo_size})) {
    usr->gdata.curr_screen = VOICE_PANEL;
  }

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 7 +
                  frame_height * 6);
  if (igButton("\ue9b6 Quit", (ImVec2){logo_size})) {
    env->config.running = false;
    save_user_settings(usrs);
  }

  draw_privacy_policy_popup(env);

  igPopFont();
}

void ui_title_screen_destroy(tenv* env) {}
