#include "skin_editor.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

#include "../user.h"
#include "../game/ntl_tags.h"
#include "../game/vlither_tags.h"
#include "skin_wheel.h"

int skin_code_filter(ImGuiInputTextCallbackData* data) {
  return !((data->EventChar >= 'a' && data->EventChar <= 'z') ||
           (data->EventChar == '-') || (data->EventChar == ',') ||
           (data->EventChar >= '0' && data->EventChar <= '9'));
}


static void apply_skin_editor_background(tenv *env) {
  if (!env || !env->usr || !env->usr->r || !env->ctx) return;
  tuser_data *usr = env->usr;
  renderer *r = usr->r;
  int selected = usr->usrs.homepage_background;
  if (selected < 0 || selected > 7) selected = 5;
  r->global.bd_opacity = 0.0f;
  r->global.minimap_opacity = 0.0f;
  r->global.bg_color[0] = r->global.bg_color[1] = r->global.bg_color[2] = 1.0f;
  if (selected == 0) {
    renderer_set_background_variant(r, env->ctx, 0);
    r->global.bg_opacity = 0.0f;
    r->global.bg_blur = 0.0f;
    return;
  }
  int variant = 16 + selected;
  renderer_set_background_variant(r, env->ctx, variant);
  if (r->bg_variant != variant || !r->active_bg_tex) {
    r->global.bg_opacity = 0.0f;
    return;
  }
  float sx = (float)env->ctx->size[0] / r->active_bg_tex->size[0];
  float sy = (float)env->ctx->size[1] / r->active_bg_tex->size[1];
  float cover = fmaxf(sx, sy);
  r->global.zoom = 1.0f;
  r->global.bg_scale = cover;
  r->global.view[0] = r->active_bg_tex->size[0] * cover * 0.5f;
  r->global.view[1] = r->active_bg_tex->size[1] * cover * 0.5f;
  r->global.bg_blur = usr->usrs.homepage_blur / 100.0f;
  r->global.bg_opacity = 1.0f;
}

typedef enum { SK_BROWSE = 0, SK_TAGS, SK_ACCESSORY, SK_BUILD } sk_mode;

#define SK_SEGMENTS 72
#define SK_STEP_RATIO (8.0f / 48.0f)

static sk_mode s_mode = SK_BROWSE;
static int s_tags_tab = 0;
static bool s_inited = false;
static float s_pos = 0.0f;
static float s_vel = 0.0f;
static float s_target = 0.0f;
static bool s_has_target = false;
static float s_anim = 0.0f;
static float s_phase = 0.0f;
static float s_drag_total = 0.0f;
static int s_last_n = 0;
static bool s_more_colors = false;

static inline float sk_clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static inline float sk_ease(float t) {
  t = sk_clampf(t, 0.0f, 1.0f);
  return 0.5f * (1.0f - cosf((float)M_PI * t));
}

static float sk_wrap_delta(float i, float pos, int n) {
  float d = fmodf(i - pos, (float)n);
  if (d < -n * 0.5f) d += (float)n;
  if (d >= n * 0.5f) d -= (float)n;
  return d;
}

static void sk_hsv(float h, float* r, float* g, float* b) {
  h = h - (float)(int)h;
  float x = h * 6.0f;
  int i = (int)x;
  float f = x - (float)i;
  float q = 1.0f - f;
  switch (i % 6) {
    case 0: *r = 1; *g = f; *b = 0; break;
    case 1: *r = q; *g = 1; *b = 0; break;
    case 2: *r = 0; *g = 1; *b = f; break;
    case 3: *r = 0; *g = q; *b = 1; break;
    case 4: *r = f; *g = 0; *b = 1; break;
    default: *r = 1; *g = 0; *b = q; break;
  }
}

static void sk_rgb_outline(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float time) {
  const float pi = 3.14159265f;
  float h = mx.y - mn.y;
  float r = h * 0.5f;
  float straight = (mx.x - mn.x) - h;
  if (straight < 0) straight = 0;
  float arc = pi * r;
  float total = 2.0f * straight + 2.0f * arc;
  const int steps = 120;
  float thick = h * 0.07f;
  if (thick < 1.5f) thick = 1.5f;
  ImVec2 prev = {0, 0};
  for (int k = 0; k <= steps; k++) {
    float d = total * (float)k / (float)steps;
    ImVec2 p;
    if (d < straight) {
      p = (ImVec2){mn.x + r + d, mn.y};
    } else if (d < straight + arc) {
      float a = -0.5f * pi + (d - straight) / r;
      p = (ImVec2){mx.x - r + cosf(a) * r, mn.y + r + sinf(a) * r};
    } else if (d < 2.0f * straight + arc) {
      p = (ImVec2){mx.x - r - (d - straight - arc), mx.y};
    } else {
      float a = 0.5f * pi + (d - 2.0f * straight - arc) / r;
      p = (ImVec2){mn.x + r + cosf(a) * r, mn.y + r + sinf(a) * r};
    }
    if (k > 0) {
      float cr, cg, cb;
      sk_hsv((float)k / (float)steps + time * 0.25f, &cr, &cg, &cb);
      ImU32 col = ((ImU32)255 << 24) | ((ImU32)(cb * 255) << 16) |
                  ((ImU32)(cg * 255) << 8) | (ImU32)(cr * 255);
      ImDrawList_AddLine(dl, prev, p, col, thick);
    }
    prev = p;
  }
}

static bool sk_has_custom(const user_settings* usrs) {
  return usrs->skin_code[0] != 0 || sk_wheel_active();
}

static int sk_slot_count(const user_settings* usrs) {
  return NUM_DEFAULT_SKINS + (sk_has_custom(usrs) ? 1 : 0);
}

static int sk_selected_slot(const user_settings* usrs) {
  if (usrs->custom_skin && sk_has_custom(usrs)) return NUM_DEFAULT_SKINS;
  int s = usrs->default_skin;
  return (s < 0 || s >= NUM_DEFAULT_SKINS) ? 0 : s;
}

static int sk_round_slot(float pos, int n) {
  int s = (int)floorf(pos + 0.5f) % n;
  return s < 0 ? s + n : s;
}

static void sk_apply_slot(user_settings* usrs, int slot) {
  if (slot >= NUM_DEFAULT_SKINS) {
    usrs->custom_skin = true;
  } else {
    usrs->custom_skin = false;
    usrs->default_skin = slot;
  }
}

static bool sk_wheel_preview(void) {
  return s_mode == SK_BUILD ? s_more_colors : sk_wheel_active();
}

static void sk_dot(tenv* env, bool imgui, float cx, float cy, float r, vec4s col) {
  if (imgui) {
    ImDrawList* dl = igGetBackgroundDrawList(igGetMainViewport());
    ImDrawList_AddCircleFilled(
        dl, (ImVec2){cx, cy}, r,
        igGetColorU32_Vec4((ImVec4){col.x, col.y, col.z, col.w}), 24);
    return;
  }
  tuser_data* usr = env->usr;
  bp_renderer_push(usr->r->bpr,
                   &(bp_instance){{cx - r, cy - r, r * 2},
                                  usr->gdata.cg_uvs[BLANK_UV],
                                  {col.x, col.y, col.z, col.w}});
}

static void sk_draw_snake(tenv* env, int slot, float cx, float cy, float size,
                          float wiggle, float alpha, bool is_main) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  bool custom = slot >= NUM_DEFAULT_SKINS;
  const bool wheel = custom && sk_wheel_preview();
  int code_len = custom ? (int)strlen(usrs->skin_code) : 0;
  int n = custom ? (code_len < SK_SEGMENTS ? code_len : SK_SEGMENTS)
                 : SK_SEGMENTS;
  if (wheel) {
    n = sk_wheel_count();
    if (n > SK_SEGMENTS) n = SK_SEGMENTS;
  }
  if (n <= 0 || size < 2.0f || alpha <= 0.01f) return;

  float step = size * SK_STEP_RATIO;
  float len = size + (n - 1) * step;
  float head_x = cx + len * 0.5f - size;
  float amp = 0.52f * size * wiggle;
  float u = size / 48.0f;

  const float wave_per_seg = 0.1208f;
  float px[SK_SEGMENTS], py[SK_SEGMENTS];
  for (int k = 0; k < n; k++) {
    float taper = 0.8f + 0.2f * ((float)(k + 1) / (float)n);
    px[k] = head_x - k * step;
    py[k] = cy - size * 0.5f + cosf(s_phase - k * wave_per_seg) * amp * taper;
  }
  float ang = n > 1 ? atan2f(py[0] - py[1], px[0] - px[1]) : 0.0f;
  float ca = cosf(ang), sa = sinf(ang);

  for (int k = n - 1; k >= 0; k--)
    bp_renderer_push(usr->r->bpr,
                     &(bp_instance){{px[k] - 10 * u, py[k] - 10 * u, 68 * u},
                                    gdata->SHADOW_UV,
                                    {0, 0, 0, 0.2f * alpha}});

  int sk_len = custom ? 1 : gdata->default_skins[slot][0];
  if (sk_len < 1) sk_len = 1;
  if (wheel) sk_wheel_draw_body(env, n, px, py, size, alpha);
  for (int k = n - 1; k >= 0 && !wheel; k--) {
    int cg_id = custom
                    ? get_cg_id(gdata, usrs->skin_code[k])
                    : gdata->default_skins[slot][1 + (k % sk_len)];
    if (cg_id == -1) continue;
    float we = custom ? 1.0f : gdata->worm_effect[k % WORM_EFFECT_LEN];
    bp_renderer_push(usr->r->bpr,
                     &(bp_instance){{px[k], py[k], size, (float)M_PI},
                                    gdata->cg_uvs[cg_id],
                                    {we, we, we, alpha}});
  }

  default_skin_data* dfs = gdata->dfs + (custom ? 0 : (1 + slot));
  float ssc = size / 29.0f;
  float ed = 6 * ssc, esp = 6 * ssc;
  float iris_r = 6 * ssc, pupil_r = dfs->pr * ssc;
  float hx = px[0] + size * 0.5f, hy = py[0] + size * 0.5f;
  for (int side = -1; side <= 1; side += 2) {
    float ox = ed, oy = side * (esp + 0.5f);
    float ex = hx + ox * ca - oy * sa, ey = hy + ox * sa + oy * ca;
    sk_dot(env, wheel, ex, ey, iris_r,
           (vec4s){dfs->ec.r, dfs->ec.g, dfs->ec.b, alpha});
    float px_ = ed + 0.5f + 2 * ssc, py_ = side * esp;
    float qx = hx + px_ * ca - py_ * sa, qy = hy + px_ * sa + py_ * ca;
    sk_dot(env, wheel, qx, qy, pupil_r,
           (vec4s){dfs->ppc.r, dfs->ppc.g, dfs->ppc.b, alpha});
  }

  if (usrs->accessory < NUM_ACCESSORIES) {
    accessory_data* acc = gdata->accessories + usrs->accessory;
    float m = size * 0.5f * acc->sc;
    float aox = acc->of * ed;
    float acx = hx + aox * ca, acy = hy + aox * sa;
    if (wheel)
      sk_wheel_draw_accessory(env, acc->uv.x, acc->uv.y, acc->uv.z, acc->uv.w,
                              acx, acy, m, ang, alpha);
    else
      bp_renderer_push(usr->r->bpr,
                       &(bp_instance){{acx - m, acy - m, m * 2, ang},
                                      acc->uv,
                                      {1, 1, 1, alpha}});
  }

  if (is_main) {
    int preview_tag_id = ntl_tags_preview_id();
    if (preview_tag_id >= 0)
      ntl_tags_draw_preview(env, preview_tag_id, hx, hy, ang, size / 48.0f);
  }
}

typedef struct { float x0, y0, x1, y1; } sk_clip;

static sk_clip sk_child_clip(void) {
  ImVec2 p, sz;
  igGetWindowPos(&p);
  igGetWindowSize(&sz);
  float sb = igGetStyle()->ScrollbarSize;
  return (sk_clip){p.x, p.y, p.x + sz.x - sb, p.y + sz.y};
}

static bool sk_in_clip(const sk_clip* c, float x, float y, float w, float h) {
  return x >= c->x0 && y >= c->y0 && x + w <= c->x1 && y + h <= c->y1;
}

static void sk_push_panel(tenv* env, float x, float y, float w, float h,
                          bool vertical_edge) {
  tuser_data* usr = env->usr;
  vec4s col = {0.10f, 0.11f, 0.13f, 0.90f};
  if (!vertical_edge) {
    float t = h, l = w + 2.0f * t;
    bp_renderer_push(usr->r->bpr, &(bp_instance){.circle = {x - t, y, l, 0},
                                                 .uv_rect = usr->gdata.cg_uvs[BLANK_UV],
                                                 .color = col,
                                                 .shape = {t, 1}});
    bp_renderer_push(usr->r->bpr, &(bp_instance){.circle = {x - t, y, l, 0},
                                                 .uv_rect = usr->gdata.cg_uvs[BLANK_UV],
                                                 .color = {1, 1, 1, 0.10f},
                                                 .shape = {2, 1}});
  } else {
    float t = w, l = h + 2.0f * t;
    float xc = x + w * 0.5f, yc = y + h * 0.5f;
    bp_renderer_push(usr->r->bpr, &(bp_instance){.circle = {xc - l * 0.5f, yc - t * 0.5f, l, (float)M_PI * 0.5f},
                                                 .uv_rect = usr->gdata.cg_uvs[BLANK_UV],
                                                 .color = col,
                                                 .shape = {t, 1}});
  }
}

static bool sk_pill(const char* label, float x, float y, float w, float h,
                    bool primary) {
  igSetCursorPos((ImVec2){x, y});
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, h * 0.5f);
  if (primary) {
    ImGuiStyle* st = igGetStyle();
    igPushStyleColor_Vec4(ImGuiCol_Button, st->Colors[ImGuiCol_ButtonActive]);
  }
  bool pressed = igButton(label, (ImVec2){w, h});
  if (primary) igPopStyleColor(1);
  igPopStyleVar(1);
  return pressed;
}

static void sk_leave(tenv* env) {
  user_settings* usrs = &env->usr->usrs;
  if (!sk_has_custom(usrs)) usrs->custom_skin = false;
  env->usr->gdata.curr_screen = TITLE_SCREEN;
  sk_wheel_save();
  save_user_settings(usrs);
  s_mode = SK_BROWSE;
  s_inited = false;
  s_anim = 0.0f;
}

static void sk_accessory_sheet(tenv* env, float scale, float panel_w) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();

  if (igButton(usrs->own_skin_invisible ? "Invisible skin: ON"
                                        : "Invisible skin: OFF",
               (ImVec2){-1, 0}))
    usrs->own_skin_invisible = !usrs->own_skin_invisible;
  igTextDisabled("Use the configured invisible-skin key to switch back during play.");
  igSeparator();

  int per_row = (int)((panel_w - style->WindowPadding.x * 2 - style->ScrollbarSize) /
                      (scale + style->ItemSpacing.x));
  if (per_row < 3) per_row = 3;

  ImVec2 origin;
  sk_clip clip = sk_child_clip();
  int total = NUM_ACCESSORIES + 1;
  for (int aid = 0; aid < total; aid++) {
    bool none = aid == NUM_ACCESSORIES;
    if (aid % per_row != 0) igSameLine(0, -1);
    igGetCursorScreenPos(&origin);

    char id[12];
    snprintf(id, sizeof id, "a%d", aid);
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){1, 0.5f, 0.5f, 1});
    if (!none) {
      igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, scale / 2);
      igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0, 0, 0, 0});
      igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0, 0, 0, 0});
      igPushStyleColor_Vec4(ImGuiCol_BorderShadow, (ImVec4){0, 0, 0, 0});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0, 0, 0, 0});
      igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0, 0, 0, 0});
    }
    igPushID_Str(id);
    bool pressed = igButton(none ? "\uea0f" : "", (ImVec2){scale, scale});
    igPopID();
    if (!none) igPopStyleVar(1);
    igPopStyleColor(none ? 1 : 6);
    if (pressed) usrs->accessory = none ? NO_ACCESSORY : aid;

    if (!none && sk_in_clip(&clip, origin.x, origin.y, scale, scale)) {
      float a = igIsItemActive() ? 0.5f : (igIsItemHovered(ImGuiHoveredFlags_None) ? 1.0f : 0.9f);
      bool selected = usrs->accessory == aid;
      if (selected)
        bp_renderer_push(usr->r->bpr,
                         &(bp_instance){{origin.x - scale * 0.12f,
                                         origin.y - scale * 0.12f, scale * 1.24f},
                                        gdata->SHADOW_UV,
                                        {1, 1, 1, 0.25f}});
      bp_renderer_push(usr->r->bpr,
                       &(bp_instance){{origin.x, origin.y, scale},
                                      gdata->accessories[aid].uv,
                                      {1, 1, 1, a}});
    }
  }
}

static void sk_build_sheet(tenv* env, float scale, float panel_w) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  float frame_height = igGetFrameHeight();
  int len = (int)strlen(usrs->skin_code);

  float btn = frame_height;
  igSetNextItemWidth(panel_w - style->WindowPadding.x * 2 -
                     style->ScrollbarSize - 2 * (btn + style->ItemSpacing.x));
  igInputTextWithHint("##ss", "Skin code", usrs->skin_code, MAX_SKIN_CODE_LEN + 1,
                      ImGuiInputTextFlags_CallbackCharFilter |
                          ImGuiInputTextFlags_EnterReturnsTrue,
                      skin_code_filter, NULL);
  igSameLine(0, -1);
  igPushStyleVarX(ImGuiStyleVar_FramePadding, 0);
  if (igButton("C", (ImVec2){btn, btn})) {
    if (s_more_colors)
      sk_wheel_undo();
    else if (len > 0)
      usrs->skin_code[len - 1] = 0;
  }
  igSameLine(0, -1);
  if (igButton("\ue9ac", (ImVec2){btn, btn})) {
    if (s_more_colors)
      sk_wheel_clear();
    else
      usrs->skin_code[0] = 0;
  }
  igPopStyleVar(1);
  len = (int)strlen(usrs->skin_code);

  {
    const char* label = s_more_colors ? "Classic Colors" : "More Colors";
    ImVec2 ts, avail;
    igCalcTextSize(&ts, label, NULL, false, -1.0f);
    igGetContentRegionAvail(&avail);
    float bw = ts.x + style->FramePadding.x * 3.0f;
    if (bw > avail.x) bw = avail.x;
    igSetCursorPosX(igGetCursorPosX() + (avail.x - bw) * 0.5f);
    igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, frame_height * 0.5f);
    if (igButton(label, (ImVec2){bw, 0})) s_more_colors = !s_more_colors;
    igPopStyleVar(1);
    ImVec2 mn, mx;
    igGetItemRectMin(&mn);
    igGetItemRectMax(&mx);
    sk_rgb_outline(igGetWindowDrawList(), mn, mx, (float)igGetTime());
  }
  if (s_more_colors) {
    sk_wheel_panel(env, panel_w);
    return;
  }
  igTextDisabled("Tap colours to add segments, head first.");

  int per_row = (int)((panel_w - style->WindowPadding.x * 2 - style->ScrollbarSize) /
                      (scale + style->ItemSpacing.x));
  if (per_row < 4) per_row = 4;
  if (per_row > 14) per_row = 14;

  sk_clip clip = sk_child_clip();
  for (int cg_id = 0; cg_id < 42; cg_id++) {
    bool is_disabled = (cg_id == 36 || cg_id == 38 || cg_id == 40 ||
                        cg_id == 41 || !(len < MAX_SKIN_CODE_LEN));
    if (cg_id % per_row != 0) igSameLine(0, -1);
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
    if (pressed && len < MAX_SKIN_CODE_LEN) {
      usrs->skin_code[len] = gdata->ntl_cg_map[cg_id];
      usrs->skin_code[len + 1] = 0;
      len++;
    }

    if (igIsItemHovered(ImGuiHoveredFlags_None)) a = 1.0f;
    if (igIsItemActive()) a = 0.6f;
    if (sk_in_clip(&clip, item_pos.x, item_pos.y, scale, scale)) {
      bp_renderer_push(usr->r->bpr,
                       &(bp_instance){{item_pos.x - scale * 0.25f,
                                       item_pos.y - scale * 0.25f, scale * 1.5f},
                                      gdata->SHADOW_UV,
                                      {0, 0, 0, 0.2f}});
      bp_renderer_push(usr->r->bpr,
                       &(bp_instance){{item_pos.x, item_pos.y, scale},
                                      gdata->cg_uvs[cg_id],
                                      {1, 1, 1, a}});
    }
  }
}

void ui_skin_editor_init(tenv* env) {}

void ui_skin_editor(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  ImGuiStyle* style = igGetStyle();
  user_settings* usrs = &usr->usrs;
  ImGuiIO* io = igGetIO_Nil();

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  apply_skin_editor_background(env);

  const float W = ctx->size[0], H = ctx->size[1];
  const bool landscape = W > H * 1.1f;
  const float frame_h = igGetFrameHeight();
  const float margin = fmaxf(12.0f, fminf(W, H) * 0.03f);
  const float btn_h = frame_h * 1.45f;
  const float btn_w = sk_clampf(fminf(W, H * 1.4f) * 0.30f, 150.0f, 340.0f);
  float dt = io->DeltaTime > 0.0f ? io->DeltaTime : 1.0f / 60.0f;
  if (dt > 0.1f) dt = 0.1f;
  const float f = dt * 60.0f;

  if (!s_inited) {
    s_inited = true;
    s_mode = SK_BROWSE;
    s_anim = 0.0f;
    s_pos = (float)sk_selected_slot(usrs);
    s_vel = 0.0f;
    s_has_target = false;
    s_last_n = sk_slot_count(usrs);
  }

  int n = sk_slot_count(usrs);
  if (n != s_last_n) {
    if (n < s_last_n && sk_round_slot(s_pos, s_last_n) >= n) {
      s_pos = 0.0f;
      s_has_target = false;
    }
    s_last_n = n;
  }
  {
    float shift = floorf(s_pos / (float)n) * (float)n;
    s_pos -= shift;
    if (s_has_target) s_target -= shift;
  }

  const bool sheet_open = s_mode != SK_BROWSE;
  s_anim += ((sheet_open ? 1.0f : 0.0f) - s_anim) *
            sk_clampf(dt * 9.0f, 0.0f, 1.0f);
  const float a = sk_ease(s_anim);
  s_phase += dt * (125.0f / 23.0f);
  if (s_phase > 6.2831853f) s_phase = fmodf(s_phase, 6.2831853f);

  float sheet_w = W, sheet_h;
  if (landscape) {
    sheet_w = sk_clampf(W * 0.46f, 340.0f, 760.0f);
    sheet_h = H;
  } else {
    sheet_h = sk_clampf(H * 0.58f, 260.0f, H * 0.7f);
  }

  float big = sk_clampf(W * 0.05f, 30.0f, 84.0f);
  if (big > H * 0.07f) big = H * 0.07f;
  const float row_h = big * 3.0f;
  const float free_w = landscape ? (W - sheet_w - margin * 2.0f) : (W - margin * 2.0f);
  const float big_open = fminf(big, free_w / 13.0f);
  const float big_now = big + (big_open - big) * a;

  const float top_bar = margin + btn_h + margin * 0.5f;
  float cx = W * 0.5f, cy = H * 0.5f;
  if (landscape) {
    cx = W * 0.5f + ((W - sheet_w) * 0.5f - W * 0.5f) * a;
  } else {
    float open_cy = top_bar + (H - sheet_h - top_bar) * 0.5f;
    cy = H * 0.5f + (open_cy - H * 0.5f) * a;
  }

  float region_y0 = top_bar;
  float region_y1 = H - (btn_h + margin * 1.5f);
  float region_w = W;
  if (sheet_open) {
    if (landscape) {
      region_y0 = 0.0f;
      region_y1 = H;
      region_w = W - sheet_w;
    } else {
      region_y1 = H - sheet_h;
    }
  }
  float region_h = region_y1 - region_y0;
  if (region_h < 20.0f) region_h = 20.0f;
  if (region_w < 20.0f) region_w = 20.0f;

  igSetCursorPos((ImVec2){0, region_y0});
  igInvisibleButton("##skin_carousel", (ImVec2){region_w, region_h}, 0);
  const bool active = igIsItemActive();
  const bool hovered = igIsItemHovered(ImGuiHoveredFlags_None);
  if (igIsItemActivated()) s_drag_total = 0.0f;

  if (s_mode == SK_BROWSE) {
    if (active) {
      float dy = io->MouseDelta.y;
      s_drag_total += fabsf(dy);
      s_pos -= dy / row_h;
      float v = (-dy / row_h) / f;
      s_vel += (v - s_vel) * 0.5f;
      s_has_target = false;
    } else {
      if (hovered && io->MouseWheel != 0.0f) {
        s_vel += -io->MouseWheel * 0.22f;
        s_has_target = false;
      }
      if (igIsItemDeactivated() && s_drag_total < 10.0f) {
        int off = (int)floorf((io->MousePos.y - cy) / row_h + 0.5f);
        if (off != 0) {
          s_target = floorf(s_pos + 0.5f) + (float)off;
          s_has_target = true;
        }
      }
      float goal = s_has_target ? s_target : floorf(s_pos + 0.5f);
      s_vel *= powf(0.925f, f);
      s_vel += (goal - s_pos) * 0.025f * f;
      s_pos += s_vel * f;
      if (s_has_target && fabsf(goal - s_pos) < 0.02f && fabsf(s_vel) < 0.01f)
        s_has_target = false;
    }
  } else {
    s_vel = 0.0f;
    s_has_target = false;
    s_pos += (floorf(s_pos + 0.5f) - s_pos) * sk_clampf(dt * 12.0f, 0.0f, 1.0f);
  }

  int sel = sk_round_slot(s_pos, n);
  if (s_mode == SK_BROWSE) sk_apply_slot(usrs, sel);

  if (s_mode == SK_BUILD) {
    if (s_more_colors ? sk_wheel_count() > 0 : usrs->skin_code[0] != 0)
      sk_draw_snake(env, NUM_DEFAULT_SKINS, cx, cy, big_now, 1.0f,
                    usrs->own_skin_invisible ? 0.25f : 1.0f, true);
  } else {
    for (int i = 0; i < n; i++) {
      float d = sk_wrap_delta((float)i, s_pos, n);
      float dist = fabsf(d);
      if (dist > 3.6f) continue;
      float sc = 0.5f + 0.5f * sk_clampf(1.0f - dist, 0.0f, 1.0f);
      bool is_sel = (i == sel);
      if (!is_sel) sc *= (1.0f - a);
      if (sc < 0.02f) continue;
      float y = cy + d * row_h;
      if (y < -big * 2 || y > H + big * 2) continue;
      float alpha = (is_sel && usrs->own_skin_invisible) ? 0.25f : 1.0f;
      sk_draw_snake(env, i, cx, y, big_now * sc, sc, alpha, is_sel);
    }
  }

  if (s_mode == SK_BROWSE) {
    if (sk_pill("Tags", margin, margin, btn_w, btn_h, false)) s_mode = SK_TAGS;
    if (sk_pill("Accessory", W - margin - btn_w, margin, btn_w, btn_h, false))
      s_mode = SK_ACCESSORY;
    if (sk_pill("OK", margin, H - margin - btn_h, btn_w, btn_h, true))
      sk_leave(env);
    if (sk_pill("Build skin", W - margin - btn_w, H - margin - btn_h, btn_w,
                btn_h, false)) {
      s_mode = SK_BUILD;
      usrs->custom_skin = true;
      s_more_colors = sk_wheel_active();
    }
  }

  if (s_mode != SK_BROWSE) {
    ImVec2 sheet_pos = landscape ? (ImVec2){W - sheet_w * a, 0.0f}
                                 : (ImVec2){0.0f, H - sheet_h * a};
    sk_push_panel(env, sheet_pos.x, sheet_pos.y, sheet_w, sheet_h, landscape);
    igSetNextWindowPos(sheet_pos, ImGuiCond_Always, (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){sheet_w, sheet_h}, ImGuiCond_Always);
    if (igBegin("##skin_sheet", NULL,
                ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse)) {
      float panel_w = igGetWindowWidth();
      float scale = sk_clampf(fminf(W, H * 1.3f) * 0.075f, 36.0f, 64.0f);

      if (s_mode == SK_TAGS) {
        float tab_w = (panel_w - style->WindowPadding.x * 2.0f -
                       style->ItemSpacing.x) * 0.5f;
        igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, frame_h * 0.5f);
        if (igButton("NTL Tags", (ImVec2){tab_w, 0})) s_tags_tab = 0;
        igSameLine(0, -1);
        if (igButton("Vlither Tags", (ImVec2){tab_w, 0})) s_tags_tab = 1;
        igPopStyleVar(1);
        igSeparator();
      } else if (s_mode == SK_BUILD) {
        igTextDisabled("Build skin");
      } else {
        igTextDisabled("Accessory");
      }

      float footer_h = btn_h + style->ItemSpacing.y;
      igBeginChild_Str("##skin_sheet_body", (ImVec2){0, -footer_h},
                       ImGuiChildFlags_None,
                       ImGuiWindowFlags_AlwaysVerticalScrollbar);
      if (s_mode == SK_TAGS) {
        if (s_tags_tab == 0)
          ntl_tags_skin_panel(env);
        else
          vlither_tags_skin_panel(env);
      } else if (s_mode == SK_ACCESSORY) {
        sk_accessory_sheet(env, scale, panel_w);
      } else {
        sk_build_sheet(env, scale, panel_w);
      }
      igEndChild();

      igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, btn_h * 0.5f);
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            style->Colors[ImGuiCol_ButtonActive]);
      float ok_w = (s_mode == SK_BUILD)
                       ? (panel_w - style->WindowPadding.x * 2 -
                          style->ItemSpacing.x) * 0.5f
                       : -1.0f;
      bool ok = igButton("OK", (ImVec2){ok_w, btn_h});
      igPopStyleColor(1);
      bool erase = false;
      if (s_mode == SK_BUILD) {
        igSameLine(0, -1);
        erase = igButton("Erase", (ImVec2){-1, btn_h});
      }
      igPopStyleVar(1);

      if (erase) {
        if (s_more_colors)
          sk_wheel_clear();
        else
          usrs->skin_code[0] = 0;
      }
      if (ok) {
        if (s_mode == SK_BUILD) {
          sk_wheel_set_active(s_more_colors && sk_wheel_count() > 0);
          sk_wheel_save();
          if (!sk_has_custom(usrs)) usrs->custom_skin = false;
        }
        s_mode = SK_BROWSE;
        s_pos = (float)sk_selected_slot(usrs);
        s_last_n = sk_slot_count(usrs);
        s_vel = 0.0f;
        s_has_target = false;
      }
    }
    igEnd();
  }

  igPopFont();
}

void ui_skin_editor_destroy(tenv* env) { sk_wheel_destroy(env); }
