#include "controls.h"

#include "../user.h"

void ui_controls_init(tenv* env) {}

void ui_controls(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  game_data* gdata = &usr->gdata;

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  float frame_height = igGetFrameHeight();
#ifdef ANDROID
  const int panel_columns = 2;
#else
  const int panel_columns = 4;
#endif

  bool swapped = usrs->ctrl_swap_sides;

  /* Centered translucent panel so the player can still see the live
     gameplay/control preview behind it. */
  float panel_w = ctx->size[0] * 0.86f;
  float panel_h = ctx->size[1] * 0.82f;
#ifdef ANDROID
  if (panel_w > 1180.0f) panel_w = 1180.0f;
  if (panel_h > ctx->size[1] - 36.0f) panel_h = ctx->size[1] - 36.0f;
#else
  if (panel_w > 1320.0f) panel_w = 1320.0f;
  if (panel_h > 920.0f) panel_h = 920.0f;
#endif
  if (panel_w < 320.0f) panel_w = ctx->size[0] - 20.0f;
  if (panel_h < 320.0f) panel_h = ctx->size[1] - 20.0f;
  float panel_x = (ctx->size[0] - panel_w) * 0.5f;
  float panel_y = (ctx->size[1] - panel_h) * 0.5f;

  igSetCursorPos((ImVec2){panel_x, panel_y});
  igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 18.0f);
  igPushStyleVar_Float(ImGuiStyleVar_ChildBorderSize, 1.0f);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){18.0f, 16.0f});
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 8.0f);
  igPushStyleColor_Vec4(ImGuiCol_ChildBg,
                        (ImVec4){0.03f, 0.06f, 0.10f, 0.58f});
  igPushStyleColor_Vec4(ImGuiCol_Border,
                        (ImVec4){0.19f, 0.40f, 0.78f, 0.38f});
  igPushStyleColor_Vec4(ImGuiCol_Separator,
                        (ImVec4){0.28f, 0.42f, 0.63f, 0.38f});

  if (igBeginChild_Str("controls_panel_root", (ImVec2){panel_w, panel_h},
                       ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav)) {
    igText("Controls");
    igSeparator();

    float footer_h = frame_height * 1.8f + style->ItemSpacing.y + 10.0f;
    if (igBeginChild_Str("controls_scroll_area", (ImVec2){0.0f, -footer_h},
                         ImGuiChildFlags_None,
                         ImGuiWindowFlags_NoBackground)) {
      ImVec2 scroll_avail;
      igGetContentRegionAvail(&scroll_avail);
      float child_window_height = scroll_avail.y;
#ifdef ANDROID
      child_window_height = (child_window_height - style->ItemSpacing.y) * 0.5f;
#endif

      if (igBeginTable("controls_table", panel_columns, ImGuiTableFlags_None,
                       (ImVec2){}, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);

        // ---- Column 0: General ----
        igTableSetColumnIndex(0);
        igBeginChild_Str("controls_general_child",
                         (ImVec2){-1, child_window_height},
                         ImGuiChildFlags_None, ImGuiWindowFlags_None);
        igSeparatorText("General");

#ifdef ANDROID
        igSeparatorText("Control scheme");
        {
          ImVec2 avail;
          igGetContentRegionAvail(&avail);
          float half_w = (avail.x - style->ItemSpacing.x) / 2.0f;

          if (!usrs->ctrl_mode_trackpad) {
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.168f, 0.468f, 0.768f, 1.0f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.268f, 0.568f, 0.868f, 1.0f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.068f, 0.368f, 0.668f, 1.0f});
          }
          if (igButton("Joystick", (ImVec2){half_w, 0.0f})) {
            usrs->ctrl_mode_trackpad = false;
          }
          if (!usrs->ctrl_mode_trackpad) igPopStyleColor(3);

          igSameLine(0, -1);

          if (usrs->ctrl_mode_trackpad) {
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.068f, 0.568f, 0.368f, 1.0f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.168f, 0.668f, 0.468f, 1.0f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.0f, 0.468f, 0.268f, 1.0f});
          }
          if (igButton("Arrow", (ImVec2){half_w, 0.0f})) {
            usrs->ctrl_mode_trackpad = true;
          }
          if (usrs->ctrl_mode_trackpad) igPopStyleColor(3);
        }
        igSpacing();
#endif

        igSeparatorText("Layout");
        igTextColored((ImVec4){0.60f, 0.60f, 0.60f, 1.0f}, "Sides:");
        igSameLine(0, 6.0f);
        if (swapped)
          igTextColored((ImVec4){0.47f, 0.71f, 1.00f, 1.0f}, "Swapped");
        else
          igTextColored((ImVec4){0.55f, 0.88f, 0.55f, 1.0f}, "Default");
        igSpacing();

        {
          ImVec2 avail;
          igGetContentRegionAvail(&avail);
          if (swapped) {
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.30f, 0.55f, 0.88f, 0.65f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.40f, 0.65f, 0.98f, 0.75f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.20f, 0.45f, 0.78f, 0.80f});
          }
          if (igButton(swapped ? "< Swap Sides >" : "> Swap Sides <",
                       (ImVec2){avail.x, 0.0f})) {
            usrs->ctrl_swap_sides = !usrs->ctrl_swap_sides;
            swapped = usrs->ctrl_swap_sides;
          }
          if (swapped) igPopStyleColor(3);
        }
        igSpacing();

        igSeparatorText("Bot");
        igCheckbox("Show bot thinking", &usrs->bot_vis);

        igEndChild();

        // ---- Column 1: Boost Button + Joystick Ring ----
        igTableSetColumnIndex(1);
        igBeginChild_Str("controls_col1_child", (ImVec2){-1, child_window_height},
                         ImGuiChildFlags_None, ImGuiWindowFlags_None);

        igSeparatorText("Boost Button");
        if (igBeginTable("boost_tbl", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
          igTableNextRow(ImGuiTableRowFlags_None, 0);
          igTableSetColumnIndex(0);
          igIndent(style->WindowPadding.x);
          igAlignTextToFramePadding();
          igText("Custom position");
          igAlignTextToFramePadding();
          igText("X");
          igAlignTextToFramePadding();
          igText("Y");
          igAlignTextToFramePadding();
          igText("Size");
          igAlignTextToFramePadding();
          igText("Opacity");

          igTableSetColumnIndex(1);
          igCheckbox("##boost custom", &usrs->boost_pos_custom);
          igBeginDisabled(!usrs->boost_pos_custom);
          igSetNextItemWidth(-1);
          igSliderFloat("##boost x", &usrs->boost_rel_x, 0.05f, 0.95f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##boost y", &usrs->boost_rel_y, 0.05f, 0.98f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##boost size", &usrs->boost_rel_size, 0.06f, 0.22f, "%.3f",
                        ImGuiSliderFlags_AlwaysClamp);
          igEndDisabled();
          igSetNextItemWidth(-1);
          igSliderFloat("##boost opacity", &usrs->boost_opacity, 0.0f, 1.0f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igIndent(-style->WindowPadding.x);
          igEndTable();
        }
        if (igButton("Reset boost position", (ImVec2){-1, 0.0f})) {
          usrs->boost_pos_custom = false;
          usrs->boost_rel_x      = swapped ? 0.125f : 0.875f;
          usrs->boost_rel_y      = 0.875f;
          usrs->boost_rel_size   = 0.125f;
        }
        igSpacing();
        igSpacing();

        igSeparatorText("Joystick Ring");
        if (igBeginTable("joy_tbl", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
          igTableNextRow(ImGuiTableRowFlags_None, 0);
          igTableSetColumnIndex(0);
          igIndent(style->WindowPadding.x);
          igAlignTextToFramePadding();
          igText("Custom position");
          igAlignTextToFramePadding();
          igText("X");
          igAlignTextToFramePadding();
          igText("Y");
          igAlignTextToFramePadding();
          igText("Size");
          igAlignTextToFramePadding();
          igText("Opacity");

          igTableSetColumnIndex(1);
          igCheckbox("##joy custom", &usrs->joy_pos_custom);
          igBeginDisabled(!usrs->joy_pos_custom);
          igSetNextItemWidth(-1);
          igSliderFloat("##joy x", &usrs->joy_rel_x, 0.05f, 0.95f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##joy y", &usrs->joy_rel_y, 0.30f, 0.98f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##joy size", &usrs->joy_rel_size, 0.08f, 0.28f, "%.3f",
                        ImGuiSliderFlags_AlwaysClamp);
          igEndDisabled();
          igSetNextItemWidth(-1);
          igSliderFloat("##joy opacity", &usrs->joy_opacity, 0.0f, 1.0f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igIndent(-style->WindowPadding.x);
          igEndTable();
        }
        if (igButton("Reset joystick position", (ImVec2){-1, 0.0f})) {
          usrs->joy_pos_custom = false;
          usrs->joy_rel_x      = swapped ? 0.875f : 0.125f;
          usrs->joy_rel_y      = 0.825f;
          usrs->joy_rel_size   = 0.175f;
        }

        igEndChild();

        // ---- Column 2: Arrow Cursor + Zoom Slider ----
#ifdef ANDROID
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);
#else
        igTableSetColumnIndex(2);
#endif
        igBeginChild_Str("controls_col2_child", (ImVec2){-1, child_window_height},
                         ImGuiChildFlags_None, ImGuiWindowFlags_None);

        igSeparatorText("Arrow Cursor");
        if (igBeginTable("arrow_tbl", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
          igTableNextRow(ImGuiTableRowFlags_None, 0);
          igTableSetColumnIndex(0);
          igIndent(style->WindowPadding.x);
          igAlignTextToFramePadding();
          igText("Size");
          igAlignTextToFramePadding();
          igText("Sensitivity");
          igAlignTextToFramePadding();
          igText("Boost arrow glow");
          igAlignTextToFramePadding();
          igText("Invisible arrow");

          igTableSetColumnIndex(1);
          igSetNextItemWidth(-1);
          igSliderFloat("##arrow size", &usrs->arrow_size, 0.40f, 2.50f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##arrow sens", &usrs->arrow_sensitivity, 0.25f, 3.00f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igCheckbox("##boost arrow anim", &usrs->boost_arrow_anim);
          igCheckbox("##arrow invisible", &usrs->arrow_invisible);
          igIndent(-style->WindowPadding.x);
          igEndTable();
        }
        if (igButton("Reset arrow cursor", (ImVec2){-1, 0.0f})) {
          usrs->arrow_size        = 1.0f;
          usrs->arrow_sensitivity = 1.0f;
          usrs->boost_arrow_anim  = true;
          usrs->arrow_invisible   = false;
        }
        igSpacing();
        igSpacing();

        igSeparatorText("Zoom Slider");
        if (igBeginTable("zoom_tbl", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
          igTableNextRow(ImGuiTableRowFlags_None, 0);
          igTableSetColumnIndex(0);
          igIndent(style->WindowPadding.x);
          igAlignTextToFramePadding();
          igText("X position");
          igAlignTextToFramePadding();
          igText("Y position");
          igAlignTextToFramePadding();
          igText("Height");
          igAlignTextToFramePadding();
          igText("Opacity");
          igAlignTextToFramePadding();
          igText("Speed");
          igAlignTextToFramePadding();
          igText("Horizontal");
          igAlignTextToFramePadding();
          igText("Hide zoom bar");

          igTableSetColumnIndex(1);
          igSetNextItemWidth(-1);
          igSliderFloat("##zoom x", &usrs->zslider_rel_x, 0.02f, 0.98f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##zoom y", &usrs->zslider_rel_y, 0.10f, 0.90f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##zoom h", &usrs->zslider_rel_h, 0.08f, 0.48f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##zoom opacity", &usrs->zslider_opacity, 0.0f, 1.0f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
          igSetNextItemWidth(-1);
          igSliderFloat("##zoom speed", &usrs->zoom_sensitivity, 0.2f, 3.0f, "%.1f",
                        ImGuiSliderFlags_AlwaysClamp);
          igCheckbox("##zoom horizontal", &usrs->zslider_horizontal);
          igCheckbox("##zoom hidden", &usrs->zslider_hidden);
          igIndent(-style->WindowPadding.x);
          igEndTable();
        }
        if (igButton("Reset zoom slider", (ImVec2){-1, 0.0f})) {
          usrs->zoom_sensitivity   = 1.0f;
          usrs->zslider_rel_x      = 0.968f;
          usrs->zslider_rel_y      = 0.500f;
          usrs->zslider_rel_h      = 0.280f;
          usrs->zslider_opacity    = 1.0f;
          usrs->zslider_horizontal = false;
          usrs->zslider_hidden     = false;
        }

        igEndChild();

        // ---- Column 3: On-Screen Buttons ----
#ifdef ANDROID
        igTableSetColumnIndex(1);
#else
        igTableSetColumnIndex(3);
#endif
        igBeginChild_Str("controls_col3_child", (ImVec2){-1, child_window_height},
                         ImGuiChildFlags_None, ImGuiWindowFlags_None);
        igSeparatorText("On-Screen Buttons");
        igTextColored((ImVec4){0.55f, 0.55f, 0.55f, 1.0f},
                      "Toggle to show a tap button in-game.");
        igSpacing();

        static const char* hk_labels[NUM_HOTKEYS] = {
          "HUD", "Show Names", "Big Food",
          "Assist", "Bot", "Menu", "Restart", "Quit"
        };
        for (int hi = 0; hi < NUM_HOTKEYS; hi++) {
          char lbl[80];
          bool shown = usrs->hk_show_btn[hi];
          snprintf(lbl, sizeof(lbl), "[%s] %s##hk%d",
                   shown ? "ON " : "OFF", hk_labels[hi], hi);
          if (shown) {
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.20f, 0.55f, 0.30f, 0.70f});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.30f, 0.65f, 0.40f, 0.80f});
          }
          if (igButton(lbl, (ImVec2){-1, 0.0f})) {
            usrs->hk_show_btn[hi] = !usrs->hk_show_btn[hi];
          }
          if (shown) igPopStyleColor(2);
        }
        igEndChild();

        igEndTable();
      }
      igEndChild();
    }

    igSeparator();
    {
      ImVec2 avail;
      igGetContentRegionAvail(&avail);
      float btn_gap = style->ItemSpacing.x;
      float btn_w = (avail.x - btn_gap) * 0.5f;
      if (btn_w < 120.0f) btn_w = (avail.x > 0.0f) ? avail.x : 120.0f;
      float btn_h = frame_height * 1.8f;

      if (igButton("Reset", (ImVec2){btn_w, btn_h})) {
        usrs->ctrl_mode_trackpad = true;

        usrs->boost_pos_custom = false;
        usrs->boost_rel_x      = 0.875f;
        usrs->boost_rel_y      = 0.875f;
        usrs->boost_rel_size   = 0.125f;
        usrs->boost_opacity    = 1.0f;

        usrs->joy_pos_custom   = false;
        usrs->joy_rel_x        = 0.125f;
        usrs->joy_rel_y        = 0.825f;
        usrs->joy_rel_size     = 0.175f;
        usrs->joy_opacity      = 1.0f;

        usrs->zoom_sensitivity   = 1.0f;
        usrs->arrow_size         = 1.0f;
        usrs->arrow_sensitivity  = 1.0f;
        usrs->boost_arrow_anim   = true;
        usrs->arrow_invisible    = false;
        usrs->bot_vis            = true;
        usrs->zslider_rel_x      = 0.968f;
        usrs->zslider_rel_y      = 0.500f;
        usrs->zslider_rel_h      = 0.280f;
        usrs->zslider_opacity    = 1.0f;
        usrs->zslider_horizontal = false;
        usrs->zslider_hidden     = false;

        for (int i = 0; i < NUM_HOTKEYS; i++) usrs->hk_show_btn[i] = false;
        usrs->ctrl_swap_sides = false;
      }
      igSameLine(0, btn_gap);
      if (igButton("OK", (ImVec2){btn_w, btn_h})) {
        save_user_settings(usrs);
        gdata->curr_screen = TITLE_SCREEN;
      }
    }
  }
  igEndChild();

  igPopStyleColor(3);
  igPopStyleVar(4);
  igPopFont();
}

void ui_controls_destroy(tenv* env) {}
