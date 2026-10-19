#include "skin_wheel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "../user.h"
#include "skin_segments_png.h"
#ifdef ANDROID
#include "../android_path.h"
#endif

#ifndef IM_COL32
#define IM_COL32(R, G, B, A)                                          \
  (((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) | \
   ((ImU32)(R)))
#endif

#define W_RING_R 146.0f
#define W_DISC_R 128.0f
#define W_PICK_MAX 107.0f
#define W_MARK_R 151.0f
#define W_EXTENT (2.0f * (W_MARK_R + 16.0f))

#define AT_W 512
#define AT_H 704
#define WHEEL_Y 272
#define DOME_Y 544
#define DOME_W 288
#define DOME_H 144

static texture* s_tex = NULL;
static VkDescriptorSet s_ds = VK_NULL_HANDLE;
static bool s_tried = false;

static float s_px = 70.0f, s_py = 0.0f;
static float s_mark = 0.0f;
static float s_br = 0.0f;
static int s_seg = 0;
static int s_drag = 0;

typedef rgb_seg wl_seg;
static wl_seg s_segs[SK_WHEEL_MAX];
static int s_nseg = 0;
static bool s_loaded = false;
static bool s_use_rgb = false;
static VkDescriptorSet s_atlas_ds = VK_NULL_HANDLE;
static bool s_atlas_tried = false;

#define WL_FILE "rgb_skin.dat"

static FILE* wl_open(const char* mode) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, (int)sizeof path, WL_FILE);
  return fopen(path, mode);
#else
  return fopen(WL_FILE, mode);
#endif
}

static void wl_load(void) {
  if (s_loaded) return;
  s_loaded = true;
  FILE* f = wl_open("rb");
  if (!f) return;
  unsigned char hdr[7];
  if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr &&
      memcmp(hdr, "RGS1", 4) == 0) {
    int use = hdr[4];
    int n = hdr[5];
    unsigned char buf[SK_WHEEL_MAX * 4];
    if (n <= SK_WHEEL_MAX && fread(buf, 1, (size_t)n * 4, f) == (size_t)n * 4) {
      int kept = 0;
      for (int i = 0; i < n; i++) {
        if (buf[i * 4] >= RGB_SKIN_SHAPES) continue;
        s_segs[kept++] = (wl_seg){buf[i * 4], buf[i * 4 + 1], buf[i * 4 + 2],
                                  buf[i * 4 + 3]};
      }
      s_nseg = kept;
      s_use_rgb = use && kept > 0;
    }
  }
  fclose(f);
}

void sk_wheel_save(void) {
  wl_load();
  FILE* f = wl_open("wb");
  if (!f) return;
  unsigned char hdr[7] = {'R', 'G', 'S', '1', (unsigned char)(s_use_rgb ? 1 : 0),
                          (unsigned char)s_nseg, 0};
  fwrite(hdr, 1, sizeof hdr, f);
  for (int i = 0; i < s_nseg; i++) {
    unsigned char b[4] = {s_segs[i].shape, s_segs[i].r, s_segs[i].g,
                          s_segs[i].b};
    fwrite(b, 1, 4, f);
  }
  fclose(f);
}

int sk_wheel_count(void) {
  wl_load();
  return s_nseg;
}
void sk_wheel_clear(void) {
  wl_load();
  s_nseg = 0;
}
void sk_wheel_undo(void) {
  wl_load();
  if (s_nseg > 0) s_nseg--;
}

bool sk_wheel_active(void) {
  wl_load();
  return s_use_rgb && s_nseg > 0;
}

void sk_wheel_set_active(bool on) {
  wl_load();
  s_use_rgb = on && s_nseg > 0;
}

int sk_wheel_export(rgb_seg* out, int max) {
  wl_load();
  if (!s_use_rgb || s_nseg <= 0) return 0;
  int n = s_nseg < max ? s_nseg : max;
  memcpy(out, s_segs, (size_t)n * sizeof(rgb_seg));
  return n;
}

const unsigned char* sk_wheel_seg_png(unsigned int* len) {
  if (len) *len = SK_SEG_PNG_LEN;
  return SK_SEG_PNG;
}

static float wl_clamp(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static float wl_ang_dist(float a, float b) {
  float d = fmodf(fabsf(a - b), (float)(2.0 * M_PI));
  return d > (float)M_PI ? (float)(2.0 * M_PI) - d : d;
}

static void wl_wheel_rgb(float x, float y, float out[3]) {
  float r = sqrtf(x * x + y * y);
  float t = wl_clamp(r - 16.0f, 0.0f, 88.0f) / 88.0f;
  float th = atan2f(y, x);
  const float centre[3] = {0.0f, (float)(2.0 * M_PI / 3.0),
                           (float)(4.0 * M_PI / 3.0)};
  for (int k = 0; k < 3; k++) {
    float w = wl_clamp(2.0f - 3.0f * wl_ang_dist(th, centre[k]) / (float)M_PI,
                       0.0f, 1.0f);
    out[k] = wl_clamp(128.0f + (256.0f * w - 128.0f) * t, 0.0f, 255.0f);
  }
}

static float wl_brightness(float th) {
  if (th <= 0.0f) {
    float d = fabsf(th + (float)(M_PI / 2.0)) / (float)(M_PI / 2.0);
    return wl_clamp(1.1f * (1.0f - d), 0.0f, 1.0f);
  }
  float d = fabsf(th - (float)(M_PI / 2.0)) / (float)(M_PI / 2.0);
  return -wl_clamp(1.1f * (1.0f - d), 0.0f, 1.0f) * 0.5f;
}

static void wl_colors(float base[3], float fin[3]) {
  wl_wheel_rgb(s_px, s_py, base);
  for (int k = 0; k < 3; k++) {
    float c = base[k];
    c += s_br >= 0.0f ? (255.0f - c) * s_br : c * s_br;
    fin[k] = wl_clamp(roundf(c), 0.0f, 255.0f);
    base[k] = roundf(base[k]);
  }
}

static ImU32 wl_pack(const float rgb[3], float a) {
  return igGetColorU32_Vec4(
      (ImVec4){rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, a});
}

static void wl_put(unsigned char* at, int x, int y, const unsigned char rgba[4]) {
  unsigned char* p = at + ((size_t)y * AT_W + x) * 4;
  memcpy(p, rgba, 4);
}

static void wl_build_atlas(unsigned char* at) {
  for (size_t i = 0; i < (size_t)AT_W * AT_H; i++) {
    at[i * 4 + 0] = at[i * 4 + 1] = at[i * 4 + 2] = 255;
    at[i * 4 + 3] = 0;
  }

  int sw = 0, sh = 0;
  unsigned char* seg = decode_texture_rgba_from_memory(SK_SEG_PNG, SK_SEG_PNG_LEN,
                                                       &sw, &sh);
  if (seg) {
    for (int y = 0; y < sh && y < SK_SEG_ROWS * SK_SEG_CELL; y++)
      for (int x = 0; x < sw && x < SK_SEG_COLS * SK_SEG_CELL; x++)
        wl_put(at, x, y, seg + ((size_t)y * sw + x) * 4);
    free_texture_rgba(seg);
  }

  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      float fx = (float)x + 0.5f - 128.0f, fy = (float)y + 0.5f - 128.0f;
      float r = sqrtf(fx * fx + fy * fy);
      float a = wl_clamp(128.0f - r + 0.5f, 0.0f, 1.0f);
      float rgb[3];
      wl_wheel_rgb(fx, fy, rgb);
      unsigned char px[4] = {(unsigned char)(rgb[0] + 0.5f),
                             (unsigned char)(rgb[1] + 0.5f),
                             (unsigned char)(rgb[2] + 0.5f),
                             (unsigned char)(a * 255.0f + 0.5f)};
      wl_put(at, x, WHEEL_Y + y, px);
    }
  }

  for (int y = 0; y < DOME_H; y++) {
    for (int x = 0; x < DOME_W; x++) {
      float fx = (float)x + 0.5f - DOME_W * 0.5f;
      float fy = (float)y + 0.5f - (float)DOME_H;
      float r = sqrtf(fx * fx + fy * fy);
      float shape = wl_clamp((float)DOME_H - r + 0.5f, 0.0f, 1.0f);
      float g = 1.0f - ((float)y + 0.5f) / (float)DOME_H;
      float a = shape * powf(wl_clamp(g, 0.0f, 1.0f), 1.2f);
      unsigned char px[4] = {255, 255, 255, (unsigned char)(a * 255.0f + 0.5f)};
      wl_put(at, x, DOME_Y + y, px);
    }
  }
}

static void wl_ensure_tex(tenv* env) {
  if (s_tried) return;
  s_tried = true;
  tuser_data* usr = env->usr;
  if (!usr || !usr->r || !env->ctx) return;

  unsigned char* at = (unsigned char*)malloc((size_t)AT_W * AT_H * 4);
  if (!at) return;
  wl_build_atlas(at);
  s_tex = create_mipmap_texture_from_rgba(env->ctx, at, AT_W, AT_H);
  free(at);
  if (!s_tex) return;
  s_ds = igImplVulkan_AddTexture(usr->r->linear_sampler, s_tex->view,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  if (!s_ds) {
    destroy_texture(env->ctx, s_tex);
    s_tex = NULL;
  }
}

void sk_wheel_destroy(tenv* env) {
  if (!env || !env->ctx) return;
  if (!s_tex && !s_atlas_ds) return;
  vkQueueWaitIdle(env->ctx->queue);
  if (s_atlas_ds) igImplVulkan_RemoveTexture(s_atlas_ds);
  s_atlas_ds = VK_NULL_HANDLE;
  s_atlas_tried = false;
  if (s_tex) {
    if (s_ds) igImplVulkan_RemoveTexture(s_ds);
    destroy_texture(env->ctx, s_tex);
  }
  s_tex = NULL;
  s_ds = VK_NULL_HANDLE;
  s_tried = false;
}

static void wl_image(ImDrawList* dl, int ax, int ay, int aw, int ah, ImVec2 p0,
                     ImVec2 p1, ImU32 col, bool flip_v) {
  ImTextureRef tr = {NULL, (ImTextureID)s_ds};
  float v0 = (float)ay / AT_H, v1 = (float)(ay + ah) / AT_H;
  if (flip_v) {
    float t = v0;
    v0 = v1;
    v1 = t;
  }
  ImDrawList_AddImage(dl, tr, p0, p1,
                      (ImVec2){(float)ax / AT_W, v0},
                      (ImVec2){(float)(ax + aw) / AT_W, v1}, col);
}

static void wl_wheel(tenv* env, float avail) {
  float maxh = env->ctx->size[1] * 0.46f;
  float side = fminf(avail, fmaxf(160.0f, maxh));
  const float u = side / W_EXTENT;

  ImVec2 p0;
  igGetCursorScreenPos(&p0);
  ImVec2 centre = {p0.x + avail * 0.5f, p0.y + side * 0.5f};

  igSetCursorScreenPos((ImVec2){p0.x + (avail - side) * 0.5f, p0.y});
  igInvisibleButton("##skin_wheel", (ImVec2){side, side}, 0);
  ImVec2 mouse;
  igGetMousePos(&mouse);
  float dx = (mouse.x - centre.x) / u, dy = (mouse.y - centre.y) / u;
  if (igIsItemActivated()) {
    float r = sqrtf(dx * dx + dy * dy);
    s_drag = r > W_DISC_R - 6.0f ? 2 : 1;
  }
  if (!igIsItemActive()) s_drag = 0;
  if (s_drag == 1) {
    float r = sqrtf(dx * dx + dy * dy);
    if (r > W_PICK_MAX) {
      dx *= W_PICK_MAX / r;
      dy *= W_PICK_MAX / r;
    }
    s_px = dx;
    s_py = dy;
  } else if (s_drag == 2) {
    s_mark = atan2f(dy, dx);
    s_br = wl_brightness(s_mark);
  }

  float base[3], fin[3];
  wl_colors(base, fin);

  ImDrawList* dl = igGetWindowDrawList();
  ImDrawList_AddCircleFilled(dl, centre, W_RING_R * u, wl_pack(base, 1.0f), 96);
  if (s_ds) {
    float rr = 1.005f * (float)DOME_H * u;
    wl_image(dl, 0, DOME_Y, DOME_W, DOME_H, (ImVec2){centre.x - rr, centre.y - rr},
             (ImVec2){centre.x + rr, centre.y}, IM_COL32(255, 255, 255, 255),
             false);
    wl_image(dl, 0, DOME_Y, DOME_W, DOME_H, (ImVec2){centre.x - rr, centre.y},
             (ImVec2){centre.x + rr, centre.y + rr}, IM_COL32(0, 0, 0, 128), true);
    wl_image(dl, 0, WHEEL_Y, 256, 256,
             (ImVec2){centre.x - W_DISC_R * u, centre.y - W_DISC_R * u},
             (ImVec2){centre.x + W_DISC_R * u, centre.y + W_DISC_R * u},
             IM_COL32(255, 255, 255, 255), false);
  } else {
    ImDrawList_AddCircleFilled(dl, centre, W_DISC_R * u,
                               IM_COL32(128, 128, 128, 255), 64);
  }
  if (fabsf(s_br) > 0.001f) {
    float c = s_br > 0.0f ? 1.0f : 0.0f;
    ImDrawList_AddCircleFilled(
        dl, centre, W_DISC_R * 1.005f * u,
        igGetColorU32_Vec4((ImVec4){c, c, c, fabsf(s_br)}), 96);
  }

  {
    float ca = cosf(s_mark), sa = sinf(s_mark);
    float er_x = ca, er_y = sa, et_x = -sa, et_y = ca;
    float r_in = 143.0f * u, r_out = 168.0f * u, w_in = 6.0f * u, w_out = 11.0f * u;
    ImVec2 q0 = {centre.x + er_x * r_in + et_x * w_in, centre.y + er_y * r_in + et_y * w_in};
    ImVec2 q1 = {centre.x + er_x * r_out + et_x * w_out, centre.y + er_y * r_out + et_y * w_out};
    ImVec2 q2 = {centre.x + er_x * r_out - et_x * w_out, centre.y + er_y * r_out - et_y * w_out};
    ImVec2 q3 = {centre.x + er_x * r_in - et_x * w_in, centre.y + er_y * r_in - et_y * w_in};
    ImDrawList_AddQuadFilled(dl, q0, q1, q2, q3, IM_COL32(255, 255, 255, 255));
    ImDrawList_AddQuad(dl, q0, q1, q2, q3, IM_COL32(0, 0, 0, 255), 2.0f);
  }

  {
    ImVec2 h = {centre.x + s_px * u, centre.y + s_py * u};
    ImDrawList_AddCircleFilled(dl, h, 22.0f * u, IM_COL32(0, 0, 0, 255), 32);
    ImDrawList_AddCircleFilled(dl, h, 19.8f * u, IM_COL32(255, 255, 255, 255), 32);
    ImDrawList_AddCircleFilled(dl, h, 18.3f * u, wl_pack(fin, 1.0f), 32);
  }
}

static void wl_cell(ImDrawList* dl, int shape, float cx, float cy, float half,
                    float rot, ImU32 col) {
  float ca = cosf(rot), sa = sinf(rot);
  const float lx[4] = {-half, half, half, -half};
  const float ly[4] = {-half, -half, half, half};
  ImVec2 p[4];
  for (int k = 0; k < 4; k++)
    p[k] = (ImVec2){cx + lx[k] * ca - ly[k] * sa, cy + lx[k] * sa + ly[k] * ca};
  int ox = (shape % SK_SEG_COLS) * SK_SEG_CELL;
  int oy = (shape / SK_SEG_COLS) * SK_SEG_CELL;
  float u0 = (float)ox / AT_W, u1 = (float)(ox + SK_SEG_CELL) / AT_W;
  float v0 = (float)oy / AT_H, v1 = (float)(oy + SK_SEG_CELL) / AT_H;
  ImTextureRef tr = {NULL, (ImTextureID)s_ds};
  ImDrawList_AddImageQuad(dl, tr, p[0], p[1], p[2], p[3], (ImVec2){u0, v0},
                          (ImVec2){u1, v0}, (ImVec2){u1, v1}, (ImVec2){u0, v1},
                          col);
}

static bool wl_shape_allowed(int shape) {
  if (s_nseg == 0) return true;
  return rgb_skin_shape_ok(shape, rgb_skin_type_of_shape(s_segs[0].shape));
}

static void wl_add_segment(int shape, const float fin[3]) {
  wl_load();
  if (s_nseg >= SK_WHEEL_MAX || !wl_shape_allowed(shape)) return;
  wl_seg s = {(unsigned char)shape,
              (unsigned char)wl_clamp(fin[0], 0.0f, 255.0f),
              (unsigned char)wl_clamp(fin[1], 0.0f, 255.0f),
              (unsigned char)wl_clamp(fin[2], 0.0f, 255.0f)};
  rgb_skin_fix_color(&s);
  s_segs[s_nseg++] = s;
}

static void wl_segments(float avail) {
  ImGuiStyle* style = igGetStyle();
  float gap = style->ItemSpacing.x;
  float cell = (avail - 2.0f * gap) / 3.0f;
  if (cell < 40.0f) cell = 40.0f;

  float base[3], fin[3];
  wl_colors(base, fin);
  ImDrawList* dl = igGetWindowDrawList();

  for (int i = 0; i < 6; i++) {
    if (i % 3 != 0) igSameLine(0, -1);
    igPushID_Int(i);
    bool pressed = igInvisibleButton("##seg", (ImVec2){cell, cell}, 0);
    igPopID();
    const bool allowed = wl_shape_allowed(i);
    if (pressed && allowed) {
      s_seg = i;
      wl_add_segment(i, fin);
    }
    ImVec2 a, b;
    igGetItemRectMin(&a);
    igGetItemRectMax(&b);
    if (i == s_seg) {
      ImDrawList_AddRectFilled(dl, a, b, IM_COL32(255, 255, 255, 28), cell * 0.12f, 0);
      ImDrawList_AddRect(dl, a, b, IM_COL32(255, 255, 255, 140), cell * 0.12f, 0, 2.0f);
    }
    float alpha = igIsItemActive() ? 0.7f : (igIsItemHovered(0) ? 1.0f : 0.92f);
    if (!allowed) alpha = 0.2f;
    ImVec2 c = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    if (s_ds) {
      wl_cell(dl, i, c.x, c.y, cell * 0.5f, (float)M_PI, wl_pack(fin, alpha));
    } else {
      ImDrawList_AddCircleFilled(dl, c, cell * 0.3f, wl_pack(fin, alpha), 32);
    }
  }
}

void sk_wheel_draw_body(tenv* env, int n, const float* px, const float* py,
                        float size, float alpha) {
  wl_load();
  if (n > s_nseg) n = s_nseg;
  if (n <= 0 || size < 2.0f || alpha <= 0.01f) return;
  wl_ensure_tex(env);
  ImDrawList* dl = igGetBackgroundDrawList(igGetMainViewport());
  const float h = size * 0.5f;
  for (int k = n - 1; k >= 0; k--) {
    float cx = px[k] + h, cy = py[k] + h;
    float dx = 1.0f, dy = 0.0f;
    if (k + 1 < n) {
      dx = cx - (px[k + 1] + h);
      dy = cy - (py[k + 1] + h);
    } else if (k > 0) {
      dx = (px[k - 1] + h) - cx;
      dy = (py[k - 1] + h) - cy;
    }
    float rot = atan2f(dy, dx) + (float)M_PI;
    float rgb[3] = {(float)s_segs[k].r, (float)s_segs[k].g, (float)s_segs[k].b};
    if (s_ds)
      wl_cell(dl, s_segs[k].shape, cx, cy, size, rot, wl_pack(rgb, alpha));
    else
      ImDrawList_AddCircleFilled(dl, (ImVec2){cx, cy}, h, wl_pack(rgb, alpha), 24);
  }
}

void sk_wheel_draw_accessory(tenv* env, float u, float v, float w, float h,
                             float cx, float cy, float half, float ang,
                             float alpha) {
  tuser_data* usr = env->usr;
  if (!s_atlas_tried) {
    s_atlas_tried = true;
    if (usr && usr->r && usr->r->tex_atlas)
      s_atlas_ds = igImplVulkan_AddTexture(usr->r->linear_sampler,
                                           usr->r->tex_atlas->view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  if (!s_atlas_ds) return;
  ImDrawList* dl = igGetBackgroundDrawList(igGetMainViewport());
  float ca = cosf(ang), sa = sinf(ang);
  const float lx[4] = {-half, half, half, -half};
  const float ly[4] = {-half, -half, half, half};
  ImVec2 p[4];
  for (int k = 0; k < 4; k++)
    p[k] = (ImVec2){cx + lx[k] * ca - ly[k] * sa, cy + lx[k] * sa + ly[k] * ca};
  ImTextureRef tr = {NULL, (ImTextureID)s_atlas_ds};
  ImDrawList_AddImageQuad(dl, tr, p[0], p[1], p[2], p[3], (ImVec2){u, v},
                          (ImVec2){u + w, v}, (ImVec2){u + w, v + h},
                          (ImVec2){u, v + h}, igGetColorU32_Vec4((ImVec4){1, 1, 1, alpha}));
}

void sk_wheel_panel(tenv* env, float panel_w) {
  ImGuiStyle* style = igGetStyle();
  wl_ensure_tex(env);

  float avail = panel_w - style->WindowPadding.x * 2.0f - style->ScrollbarSize;
  if (avail < 120.0f) avail = 120.0f;

  igTextDisabled("Tap a shape to add it to the snake, head first.");
  igTextDisabled("Ball and dark ball can be mixed; the other shapes work alone.");
  wl_wheel(env, avail);
  igSpacing();
  wl_segments(avail);
  igSpacing();
  igText("Segments: %d / %d", s_nseg, SK_WHEEL_MAX);
  igSameLine(0, -1);
  if (igButton("Undo", (ImVec2){0, 0})) sk_wheel_undo();
  igSameLine(0, -1);
  if (igButton("Clear", (ImVec2){0, 0})) sk_wheel_clear();
}
