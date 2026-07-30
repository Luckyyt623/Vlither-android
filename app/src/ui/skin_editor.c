#include "skin_editor.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

#include "../user.h"

int skin_code_filter(ImGuiInputTextCallbackData* data) {
  return !((data->EventChar >= 'a' && data->EventChar <= 'z') ||
           (data->EventChar == '-') || (data->EventChar == ',') ||
           (data->EventChar >= '0' && data->EventChar <= '9'));
}

void ui_skin_editor_init(tenv* env) {}

void ui_skin_editor(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  ImGuiStyle* style = igGetStyle();
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  usr->r->global.bg_opacity = 0;
  usr->r->global.bd_opacity = 0;
  usr->r->global.minimap_opacity = 0;

  float frame_height = igGetFrameHeight();
  float resolution_scale = fminf(ctx->size[0] / 1280.0f, ctx->size[1] / 720.0f);
  if (resolution_scale < 0.70f) resolution_scale = 0.70f;
  if (resolution_scale > 1.25f) resolution_scale = 1.25f;
  float scale = 48.0f * resolution_scale;

  /* The 256-segment preview is very wide. Shrink it only when necessary so
     the editor remains fully visible on lower-resolution devices. */
  float preview_units = 1.0f + (MAX_SKIN_CODE_LEN / 2.0f - 1.0f) * (8.0f / 48.0f);
  float max_scale_for_width = (ctx->size[0] * 0.94f) / preview_units;
  if (scale > max_scale_for_width) scale = max_scale_for_width;

  vec2 tot_size = {style->ItemSpacing.x * 6 + 7 * scale,
                   style->ItemSpacing.y * 5 + 6 * scale};
  float min_grid_y = style->WindowPadding.y +
                     (style->ItemSpacing.y + frame_height) * 3.0f +
                     (scale + style->ItemSpacing.y) * 2.0f;
  float grid_y = ctx->size[1] * 0.5f - tot_size[1] * 0.5f;
  if (grid_y < min_grid_y) grid_y = min_grid_y;

  float sk_w = scale + (8 * (scale / 48)) * ((MAX_SKIN_CODE_LEN / 2.0f) - 1);

  igSetCursorPosX(ctx->size[0] * 0.5 - sk_w * 0.5f);
  igSetCursorPosY((grid_y) -
                  ((style->ItemSpacing.y + frame_height) * 3 + scale +
                   style->ItemSpacing.y));

  for (int i = strlen(usrs->skin_code); i < MAX_SKIN_CODE_LEN + 1; i++)
    usrs->skin_code[i] = 0;

  for (int i = (MAX_SKIN_CODE_LEN / 2) - 1; i >= 0; i--) {
    bp_renderer_push(
        usr->r->bpr,
        &(bp_instance){
            {(igGetCursorPosX() + i * (8 * (scale / 48))) - 10 * (scale / 48),
             igGetCursorPosY() - 10 * (scale / 48), 68 * (scale / 48)},
            gdata->SHADOW_UV,
            {0, 0, 0, 0.2f}});
  }

  int cbp = 0;
  for (int i = (MAX_SKIN_CODE_LEN / 2) - 1; i >= 0; i--) {
    int cg_id;
    if (usrs->custom_skin) {
      cg_id =
          get_cg_id(&usr->gdata, usrs->skin_code[MAX_SKIN_CODE_LEN - 1 - cbp]);
    } else {
      int sk_len = gdata->default_skins[usrs->default_skin][0];
      cg_id =
          gdata->default_skins[usrs->default_skin]
                              [1 + ((MAX_SKIN_CODE_LEN - 1 - cbp) % sk_len)];
    }

    if (cg_id != -1) {
      float we =
          gdata->worm_effect[(MAX_SKIN_CODE_LEN - 1 - cbp) % WORM_EFFECT_LEN] *
              (!usrs->custom_skin) +
          usrs->custom_skin;
      bp_renderer_push(usr->r->bpr, &(bp_instance){{igGetCursorPosX() +
                                                        i * (8 * (scale / 48)),
                                                    igGetCursorPosY(), scale},
                                                   gdata->cg_uvs[cg_id],
                                                   {we, we, we, 1}});
    }

    cbp++;
  }

  igSetCursorPosX(ctx->size[0] * 0.5 - sk_w * 0.5f);
  igSetCursorPosY((grid_y) -
                  ((style->ItemSpacing.y + frame_height) * 3 +
                   2 * (scale + style->ItemSpacing.y)));

  for (int i = 0; i < MAX_SKIN_CODE_LEN / 2; i++) {
    bp_renderer_push(
        usr->r->bpr,
        &(bp_instance){
            {(igGetCursorPosX() + i * (8 * (scale / 48))) - 10 * (scale / 48),
             igGetCursorPosY() - 10 * (scale / 48), 68 * (scale / 48)},
            gdata->SHADOW_UV,
            {0, 0, 0, 0.2f}});
  }

  vec2 last_bp_pos;

  for (int i = 0; i < MAX_SKIN_CODE_LEN / 2; i++) {
    int cg_id;
    if (usrs->custom_skin) {
      cg_id =
          get_cg_id(&usr->gdata, usrs->skin_code[MAX_SKIN_CODE_LEN - 1 - cbp]);
    } else {
      int sk_len = gdata->default_skins[usrs->default_skin][0];
      cg_id =
          gdata->default_skins[usrs->default_skin]
                              [1 + ((MAX_SKIN_CODE_LEN - 1 - cbp) % sk_len)];
    }

    last_bp_pos[0] = igGetCursorPosX() + i * (8 * (scale / 48));
    last_bp_pos[1] = igGetCursorPosY();

    if (cg_id != -1) {
      float we =
          gdata->worm_effect[(MAX_SKIN_CODE_LEN - 1 - cbp) % WORM_EFFECT_LEN] *
              (!usrs->custom_skin) +
          usrs->custom_skin;
      bp_renderer_push(usr->r->bpr, &(bp_instance){{last_bp_pos[0],
                                                    last_bp_pos[1], scale, PI},
                                                   gdata->cg_uvs[cg_id],
                                                   {we, we, we, 1}});
    }

    cbp++;
  }

  float ssc = scale / 29;
  float ed = 6 * ssc;
  float esp = 6 * ssc;
  float er = 6;
  default_skin_data* dfs =
      gdata->dfs + ((1 - usrs->custom_skin) * (1 + usrs->default_skin));
  float pr = dfs->pr;
  float iris_r = er * ssc;
  float pupil_r = pr * ssc;

  float ex = ed;
  float ey = -esp - .5;

  bp_renderer_push(
      usr->r->bpr,
      &(bp_instance){{((last_bp_pos[0] + scale / 2) + ex) - iris_r,
                      ((last_bp_pos[1] + scale / 2) + ey) - iris_r, iris_r * 2},
                     gdata->cg_uvs[BLANK_UV],
                     {dfs->ec.r, dfs->ec.g, dfs->ec.b, 1}});

  ex = ed;
  ey = esp + .5;

  bp_renderer_push(
      usr->r->bpr,
      &(bp_instance){{((last_bp_pos[0] + scale / 2) + ex) - iris_r,
                      ((last_bp_pos[1] + scale / 2) + ey) - iris_r, iris_r * 2},
                     gdata->cg_uvs[BLANK_UV],
                     {dfs->ec.r, dfs->ec.g, dfs->ec.b, 1}});

  ex = ed + .5 + 2 * ssc;
  ey = -esp;

  bp_renderer_push(
      usr->r->bpr,
      &(bp_instance){
          {((last_bp_pos[0] + scale / 2) + ex) - pupil_r,
           ((last_bp_pos[1] + scale / 2) + ey) - pupil_r, pupil_r * 2},
          gdata->cg_uvs[BLANK_UV],
          {dfs->ppc.r, dfs->ppc.g, dfs->ppc.b, 1}});

  ex = (ed + .5) + 2 * ssc;
  ey = esp;

  bp_renderer_push(
      usr->r->bpr,
      &(bp_instance){
          {((last_bp_pos[0] + scale / 2) + ex) - pupil_r,
           ((last_bp_pos[1] + scale / 2) + ey) - pupil_r, pupil_r * 2},
          gdata->cg_uvs[BLANK_UV],
          {dfs->ppc.r, dfs->ppc.g, dfs->ppc.b, 1}});

  if (usrs->accessory < NUM_ACCESSORIES) {
    accessory_data* acc = gdata->accessories + usrs->accessory;
    ex = acc->of * ed;
    ey = 0;
    float m = scale * 0.5f * acc->sc;
    float acx = (ex + last_bp_pos[0] + scale / 2);
    float acy = (ey + last_bp_pos[1] + scale / 2);

    bp_renderer_push(
        usr->r->bpr,
        &(bp_instance){{acx - m, acy - m, m * 2, 0}, acc->uv, {1, 1, 1, 1}});
  }

  igSetCursorPosX(ctx->size[0] * 0.5 - tot_size[0] * 0.5f);
  igSetCursorPosY((grid_y) -
                  ((style->ItemSpacing.y + frame_height) * 3));
  if (usrs->custom_skin) {
    igSetNextItemWidth(tot_size[0] - 2 * (frame_height + style->ItemSpacing.x));
    igInputTextWithHint("##ss", "Skin code", usrs->skin_code,
                        MAX_SKIN_CODE_LEN + 1,
                        ImGuiInputTextFlags_CallbackCharFilter |
                            ImGuiInputTextFlags_EnterReturnsTrue,
                        skin_code_filter, NULL);
    igSameLine(0, -1);
    igPushStyleVarX(ImGuiStyleVar_FramePadding, 0);
    int skin_code_len = strlen(usrs->skin_code);
    if (igButton("C", (ImVec2){frame_height, frame_height}) &&
        skin_code_len > 0) {
      usrs->skin_code[strlen(usrs->skin_code) - 1] = 0;
    }
    igSameLine(0, -1);
    if (igButton("\ue9ac", (ImVec2){frame_height, frame_height})) {
      usrs->skin_code[0] = 0;
    }
    igPopStyleVar(1);
  } else {
    if (igButton("\uea38",
                 (ImVec2){tot_size[0] / 2 - style->ItemSpacing.x / 2, 0}))
      usrs->default_skin =
          (usrs->default_skin + (NUM_DEFAULT_SKINS - 1)) % NUM_DEFAULT_SKINS;
    igSameLine(0, -1);
    if (igButton("\uea34",
                 (ImVec2){tot_size[0] / 2 - style->ItemSpacing.x / 2, 0}))
      usrs->default_skin = (usrs->default_skin + 1) % NUM_DEFAULT_SKINS;
  }
  igSetCursorPosX(ctx->size[0] * 0.5 - tot_size[0] * 0.5f);
  igSetCursorPosY((grid_y) -
                  ((style->ItemSpacing.y + frame_height) * 2));
  if (igButton(usrs->custom_skin ? "\uea40 Default" : "\ue905 Custom",
               (ImVec2){tot_size[0]}))
    usrs->custom_skin = !usrs->custom_skin;

  igSetCursorPosX(ctx->size[0] * 0.5 - tot_size[0] * 0.5f);
  igSetCursorPosY((grid_y) -
                  ((style->ItemSpacing.y + frame_height) * 1));
  if (igButton("OK", (ImVec2){tot_size[0]})) {
    if (usrs->skin_code[0] == 0) usrs->custom_skin = false;
    gdata->curr_screen = TITLE_SCREEN;
    save_user_settings(usrs);
  }

  if (usrs->custom_skin) {
    float panel_w = tot_size[0] + style->ScrollbarSize + style->WindowPadding.x * 2.0f;
    if (panel_w > ctx->size[0] - style->WindowPadding.x * 2.0f)
      panel_w = ctx->size[0] - style->WindowPadding.x * 2.0f;
    float panel_x = ctx->size[0] * 0.5f - panel_w * 0.5f;
    float panel_h = ctx->size[1] - grid_y - style->WindowPadding.y;
    if (panel_h < scale * 2.25f) panel_h = scale * 2.25f;

    igSetCursorPosX(panel_x);
    igSetCursorPosY(grid_y);
    bool child_visible = igBeginChild_Str(
        "custom_skin_scroll_panel", (ImVec2){panel_w, panel_h},
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoMove);

    if (child_visible) {
      float clip_x1 = panel_x;
      float clip_y1 = grid_y;
      float clip_x2 = panel_x + panel_w - style->ScrollbarSize;
      float clip_y2 = grid_y + panel_h;
      int skin_code_len = strlen(usrs->skin_code);

      igTextDisabled("Snake colours");
      float colors_y = igGetCursorPosY() + style->ItemSpacing.y;

      int cg_id = 0;
      for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 7; j++) {
          bool is_disabled =
              (cg_id == 36 || cg_id == 38 || cg_id == 40 || cg_id == 41 ||
               !(skin_code_len < MAX_SKIN_CODE_LEN));

          igSetCursorPosY(colors_y + (style->ItemSpacing.y + scale) * i);
          igSetCursorPosX((style->ItemSpacing.x + scale) * j);
          ImVec2 item_pos;
          igGetCursorScreenPos(&item_pos);
          bool ct = gdata->cg_colors_ct[cg_id];

          float a = 0.8f * (1.0f - 0.5f * is_disabled);
          igBeginDisabled(is_disabled);
          char label[2] = {gdata->ntl_cg_map[cg_id], 0};
          igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, scale / 2);
          igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){ct, ct, ct, 1});
          igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_BorderShadow, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0, 0, 0, 0});
          igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0, 0, 0, 0});
          igPushID_Int(cg_id);
          bool pressed = igButton(label, (ImVec2){scale, scale});
          igPopID();
          igPopStyleVar(1);
          igPopStyleColor(6);
          igEndDisabled();
          if (pressed && strlen(usrs->skin_code) < MAX_SKIN_CODE_LEN)
            usrs->skin_code[strlen(usrs->skin_code)] = gdata->ntl_cg_map[cg_id];

          bool active = igIsItemActive();
          bool hovered = igIsItemHovered(ImGuiHoveredFlags_None);
          if (hovered) a = 1.0f;
          if (active) a = 0.6f;

          bool sprite_visible = item_pos.x + scale >= clip_x1 && item_pos.x <= clip_x2 &&
                                item_pos.y + scale >= clip_y1 && item_pos.y <= clip_y2;
          if (sprite_visible) {
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x - scale * 0.25f,
                                             item_pos.y - scale * 0.25f,
                                             scale * 1.5f},
                                            gdata->SHADOW_UV,
                                            {0, 0, 0, 0.2f}});
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x, item_pos.y, scale},
                                            gdata->cg_uvs[cg_id],
                                            {1, 1, 1, a}});
          }
          cg_id++;
        }
      }

      float accessories_label_y = colors_y +
          (style->ItemSpacing.y + scale) * 6.0f + style->ItemSpacing.y * 2.0f;
      igSetCursorPosX(0);
      igSetCursorPosY(accessories_label_y);
      igTextDisabled("Accessories");
      float accessories_y = igGetCursorPosY() + style->ItemSpacing.y;

      int aid = 0;
      for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 7; j++) {
          if (aid > NUM_ACCESSORIES) break;
          bool is_no_accessory = aid == NUM_ACCESSORIES;
          igSetCursorPosY(accessories_y + (style->ItemSpacing.y + scale) * i);
          igSetCursorPosX((style->ItemSpacing.x + scale) * j);
          ImVec2 item_pos;
          igGetCursorScreenPos(&item_pos);
          float a = 0.8f;

          char label[12] = {0};
          sprintf(label, "a%d", aid);
          igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){1, 0.5f, 0.5f, 1});
          if (!is_no_accessory) {
            igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, scale / 2);
            igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_BorderShadow, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0, 0, 0, 0});
            igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0, 0, 0, 0});
          }
          igPushID_Str(label);
          bool pressed = igButton(is_no_accessory ? "\uea0f" : "",
                                  (ImVec2){scale, scale});
          igPopID();
          if (!is_no_accessory) igPopStyleVar(1);
          igPopStyleColor(is_no_accessory ? 1 : 6);
          if (pressed) usrs->accessory = is_no_accessory ? NO_ACCESSORY : aid;

          bool active = igIsItemActive();
          bool hovered = igIsItemHovered(ImGuiHoveredFlags_None);
          if (hovered) a = 1.0f;
          if (active) a = 0.0f;

          bool sprite_visible = item_pos.x + scale >= clip_x1 && item_pos.x <= clip_x2 &&
                                item_pos.y + scale >= clip_y1 && item_pos.y <= clip_y2;
          if (!is_no_accessory && sprite_visible)
            bp_renderer_push(usr->r->bpr,
                             &(bp_instance){{item_pos.x, item_pos.y, scale},
                                            gdata->accessories[aid].uv,
                                            {1, 1, 1, a}});
          aid++;
        }
      }

      igSetCursorPosX(0);
      igSetCursorPosY(accessories_y + (style->ItemSpacing.y + scale) * 5.0f);
      igDummy((ImVec2){tot_size[0], style->ItemSpacing.y});
    }
    igEndChild();
  }

  igPopFont();
}

void ui_skin_editor_destroy(tenv* env) {}
