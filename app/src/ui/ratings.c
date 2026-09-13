#include "ratings.h"

#include "../game/vlither_ratings.h"
#include "../imgui_setup.h"
#include "../user.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int editor_rating = 0;
static char editor_message[VLITHER_RATING_MESSAGE_MAX + 1];
static bool editor_synced = false;
static int pending_report = -1;
static bool report_popup_requested = false;
static char reply_draft[VLITHER_RATING_REPLY_TEXT_MAX + 1];

void ui_ratings_panel_open(void) {
  editor_synced = false;
  pending_report = -1;
  report_popup_requested = false;
  reply_draft[0] = 0;
  vlither_ratings_mark_admin_replies_seen();
  vlither_ratings_refresh();
}

static void sync_editor(void) {
  if (editor_synced || !vlither_ratings_loaded()) return;
  editor_rating = vlither_ratings_mine_value();
  strncpy(editor_message, vlither_ratings_mine_message(),
          sizeof editor_message - 1);
  editor_message[sizeof editor_message - 1] = 0;
  editor_synced = true;
}

static void format_review_date(long long timestamp_ms,
                               char *out, size_t out_size) {
  if (!out || !out_size) return;
  if (timestamp_ms <= 0) {
    snprintf(out, out_size, "Recently");
    return;
  }
  time_t seconds = (time_t)(timestamp_ms / 1000LL);
  struct tm local_value;
#ifdef _WIN32
  localtime_s(&local_value, &seconds);
#else
  localtime_r(&seconds, &local_value);
#endif
  if (!strftime(out, out_size, "%b %d, %Y", &local_value))
    snprintf(out, out_size, "Recently");
}

static void draw_editor(float scale) {
  ImGuiStyle *style = igGetStyle();
  const float touch_h = igGetFrameHeight() * 1.18f;
  igTextColored((ImVec4){0.95f, 0.78f, 0.25f, 1.0f},
                vlither_ratings_has_mine() ? "Update your review"
                                           : "Rate the mod");
  igTextDisabled("Choose one rating");

  ImVec2 available;
  igGetContentRegionAvail(&available);
  float star_w = (available.x - style->ItemSpacing.x * 4.0f) / 5.0f;
  if (star_w < 42.0f * scale) star_w = 42.0f * scale;
  for (int value = 1; value <= 5; ++value) {
    igPushID_Int(value);
    bool selected = value == editor_rating;
    if (selected) {
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.78f, 0.53f, 0.08f, 0.95f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                            (ImVec4){0.92f, 0.67f, 0.14f, 1.0f});
    }
    char label[12];
    snprintf(label, sizeof label, "%d / 5", value);
    if (igButton(label, (ImVec2){star_w, touch_h})) editor_rating = value;
    if (selected) igPopStyleColor(2);
    if (value < 5) igSameLine(0, style->ItemSpacing.x);
    igPopID();
  }

  igSpacing();
  igText("Your message");
  igInputTextMultiline("##rating_message", editor_message,
                       sizeof editor_message,
                       (ImVec2){-1, 122.0f * scale},
                       ImGuiInputTextFlags_None, NULL, NULL);
  igTextDisabled("%d / %d characters", (int)strlen(editor_message),
                 VLITHER_RATING_MESSAGE_MAX);
  bool busy = vlither_ratings_loading();
  if (busy) igBeginDisabled(true);
  if (igButton(vlither_ratings_has_mine() ? "Update review"
                                         : "Publish review",
               (ImVec2){-1, touch_h}))
    vlither_ratings_submit(editor_rating, editor_message);
  if (busy) igEndDisabled();
  igSpacing();
  igPushTextWrapPos(0.0f);
  igTextDisabled("%s", vlither_ratings_status());
  igPopTextWrapPos();
  igSpacing();
  igTextDisabled("One review per device. Your private edit key stays on this phone.");
}

static void draw_reviews(float scale) {
  const int count = vlither_ratings_count();
  if (!vlither_ratings_loaded() && vlither_ratings_loading()) {
    igTextDisabled("Loading player reviews...");
    return;
  }
  if (count <= 0) {
    igTextDisabled("No public reviews yet. Be the first player to rate Vlither.");
    return;
  }
  for (int i = 0; i < count; ++i) {
    igPushID_Int(i + 1000);
    igSeparator();
    igTextColored((ImVec4){0.78f, 0.88f, 1.0f, 1.0f}, "%s",
                  vlither_ratings_nickname_at(i));
    igSameLine(0, -1);
    igTextColored((ImVec4){0.96f, 0.77f, 0.22f, 1.0f}, "%d / 5",
                  vlither_ratings_value_at(i));
    if (vlither_ratings_is_mine_at(i)) {
      igSameLine(0, -1);
      igTextColored((ImVec4){0.28f, 0.90f, 0.55f, 1.0f}, "Your review");
    }
    char date[48];
    format_review_date(vlither_ratings_updated_at_ms(i), date, sizeof date);
    igTextDisabled("%s", date);
    igPushTextWrapPos(0.0f);
    igTextWrapped("%s", vlither_ratings_message_at(i));
    igPopTextWrapPos();

    /* Reply thread is public — every player can read admin/player replies. */
    int rc = vlither_ratings_reply_count_at(i);
    for (int r = 0; r < rc; ++r) {
      const char *role = vlither_ratings_reply_role_at(i, r);
      bool is_admin = role && !strcmp(role, "admin");
      igIndent(12.0f * scale);
      igPushStyleColor_Vec4(ImGuiCol_ChildBg,
                            is_admin ? (ImVec4){0.12f, 0.22f, 0.34f, 0.55f}
                                     : (ImVec4){0.16f, 0.17f, 0.20f, 0.45f});
      igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 10.0f * scale);
      char child_id[32];
      snprintf(child_id, sizeof child_id, "##reply_%d_%d", i, r);
      igBeginChild_Str(child_id, (ImVec2){-1.0f, 0}, ImGuiChildFlags_AutoResizeY,
                       ImGuiWindowFlags_NoScrollbar);
      if (is_admin) {
        igTextColored((ImVec4){0.20f, 0.78f, 1.0f, 1.0f}, "Lucky");
        igSameLine(0, 6);
        igTextColored((ImVec4){0.35f, 0.85f, 0.55f, 1.0f}, "Developer");
      } else {
        igTextColored((ImVec4){0.88f, 0.90f, 0.96f, 1.0f}, "%s",
                      vlither_ratings_nickname_at(i));
      }
      char rdate[48];
      format_review_date(vlither_ratings_reply_time_ms_at(i, r), rdate,
                         sizeof rdate);
      igSameLine(0, 8);
      igTextDisabled("%s", rdate);
      igPushTextWrapPos(0.0f);
      igTextWrapped("%s", vlither_ratings_reply_message_at(i, r));
      igPopTextWrapPos();
      igEndChild();
      igPopStyleVar(1);
      igPopStyleColor(1);
      igUnindent(12.0f * scale);
    }

    if (vlither_ratings_is_mine_at(i) && vlither_ratings_can_reply_at(i)) {
      igSpacing();
      igSetNextItemWidth(-1.0f);
      igInputTextWithHint("##reply_draft", "Reply to developer...", reply_draft,
                          sizeof reply_draft, ImGuiInputTextFlags_None, NULL,
                          NULL);
      if (vlither_ratings_loading()) igBeginDisabled(true);
      if (igButton("Send reply", (ImVec2){120.0f * scale, 0})) {
        if (vlither_ratings_reply(reply_draft)) reply_draft[0] = 0;
      }
      if (vlither_ratings_loading()) igEndDisabled();
    } else if (!vlither_ratings_is_mine_at(i)) {
      if (vlither_ratings_loading()) igBeginDisabled(true);
      if (igButton("Report##review", (ImVec2){118.0f * scale, 0})) {
        pending_report = i;
        report_popup_requested = true;
      }
      if (vlither_ratings_loading()) igEndDisabled();
    }
    igSpacing();
    igPopID();
  }
}

static void draw_report_popup(float scale) {
  igSetNextWindowSize((ImVec2){360.0f * scale, 0}, ImGuiCond_Appearing);
  if (!igBeginPopupModal("Report review?", NULL,
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings))
    return;
  igTextWrapped("Send this review to the Vlither moderators for checking?");
  igSpacing();
  if (igButton("Cancel", (ImVec2){150.0f * scale, 0})) {
    pending_report = -1;
    igCloseCurrentPopup();
  }
  igSameLine(0, -1);
  if (igButton("Report", (ImVec2){150.0f * scale, 0})) {
    if (pending_report >= 0) vlither_ratings_report(pending_report);
    pending_report = -1;
    igCloseCurrentPopup();
  }
  igEndPopup();
}

void ui_ratings_panel(tenv *env) {
  if (!env || !env->usr) return;
  tuser_data *usr = env->usr;
  ImGuiStyle *style = igGetStyle();
  const float scale = imgui_get_ui_scale();

  sync_editor();
  igPushFont(usr->imgui_data.regular_font[usr->usrs.ui_font_size],
             usr->imgui_data.regular_font[usr->usrs.ui_font_size]->LegacySize);
  igTextColored((ImVec4){0.48f, 0.72f, 1.0f, 1.0f}, "Ratings & Reviews");
  if (vlither_ratings_total() > 0)
    igText("%.1f / 5 from %d player%s", vlither_ratings_average(),
           vlither_ratings_total(), vlither_ratings_total() == 1 ? "" : "s");
  else
    igTextDisabled("Community feedback from Vlither players");

  ImVec2 nav_avail;
  igGetContentRegionAvail(&nav_avail);
  float nav_w = (nav_avail.x - style->ItemSpacing.x) * 0.5f;
  if (igButton("Back to homepage##ratings", (ImVec2){nav_w, 0})) {
    usr->gdata.curr_screen = TITLE_SCREEN;
    igPopFont();
    return;
  }
  igSameLine(0, style->ItemSpacing.x);
  if (vlither_ratings_loading()) igBeginDisabled(true);
  if (igButton("Refresh reviews", (ImVec2){nav_w, 0}))
    vlither_ratings_refresh();
  if (vlither_ratings_loading()) igEndDisabled();
  igSeparator();

  ImVec2 body;
  igGetContentRegionAvail(&body);
  bool wide = body.x >= 760.0f * scale;
  if (wide) {
    float editor_w = body.x * 0.38f;
    igBeginChild_Str("##ratings_editor", (ImVec2){editor_w, 0},
                     ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
    draw_editor(scale);
    igEndChild();
    igSameLine(0, style->ItemSpacing.x);
    igBeginChild_Str("##ratings_feed", (ImVec2){0, 0},
                     ImGuiChildFlags_Borders,
                     ImGuiWindowFlags_AlwaysVerticalScrollbar);
    igTextColored((ImVec4){0.72f, 0.82f, 0.96f, 1.0f}, "Player reviews");
    draw_reviews(scale);
    igEndChild();
  } else {
    igBeginChild_Str("##ratings_narrow", (ImVec2){0, 0},
                     ImGuiChildFlags_Borders,
                     ImGuiWindowFlags_AlwaysVerticalScrollbar);
    draw_editor(scale);
    igSpacing();
    igSeparatorText("Player reviews");
    draw_reviews(scale);
    igEndChild();
  }
  if (report_popup_requested) {
    igOpenPopup_Str("Report review?", 0);
    report_popup_requested = false;
  }
  draw_report_popup(scale);
  igPopFont();
}
