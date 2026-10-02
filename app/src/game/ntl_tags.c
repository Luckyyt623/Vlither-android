#include "ntl_tags.h"

#include "ntl_tags_data.h"
#include "ntl_tags_atlas_data.h"
#include "ntl_team.h"
#include "tag_follow.h"
#include "../user.h"
#include "../rendering/texture.h"
#include "../cimgui/cimgui_impl.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTL_TAG_WS_URL "ws://ws.ntl-slither.com:9000"
#define NTL_TAG_HOST "ntl-slither.com"
#define NTL_TAG_AUTH_PATH "/tags/tag"
#define NTL_TAG_CATALOG_PATH "/tags/cstags"
#define NTL_TAG_FIXED_CATALOG_PATH "/tags/fstags"
#define NTL_TAG_RECONNECT_SECONDS 3.0
#define NTL_TAG_POSITION_SECONDS 1.0
#define NTL_TAG_CATALOG_RETRY_SECONDS 60.0
#define NTL_TAG_CATALOG_REFRESH_SECONDS 3600.0
#define NTL_TAG_CATALOG_TIMEOUT_SECONDS 45.0
#define NTL_DYNAMIC_TAG_MAX 277
#define NTL_FIXED_TAG_MAX 96
#define NTL_DYNAMIC_ATLAS_SIZE 1024
#define NTL_DYNAMIC_ATLAS_COLUMNS 17
#define NTL_DYNAMIC_ATLAS_CELL 60
#define NTL_DYNAMIC_ATLAS_PADDING 3
#define NTL_TAG_CATALOG_MAX_BYTES (12u * 1024u * 1024u)

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) \
  (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0))
#endif

typedef struct ntl_tags_state {
  bool ready;
  tenv *env;
  struct mg_mgr mgr;
  struct mg_connection *ws;
  struct mg_connection *auth;
  struct mg_connection *catalog;
  struct mg_connection *fixed_catalog;
  bool ws_open;
  bool handshake_sent;
  double next_connect;
  double last_position_send;
  char active_server[MAX_SERVER_IP_LEN + 1];
  uint16_t active_ntl_id;

  int pending_tag_id;
  int preview_tag_id;
  char pending_hash[33];
  char pending_password[96];
  double auth_started;
  double catalog_started;
  double next_catalog_sync;
  long catalog_version;
  double fixed_catalog_started;
  double next_fixed_catalog_sync;
  long fixed_catalog_version;
  int public_tag_max;
  int private_tag_min;
  int dynamic_tag_count;
  ntl_tag_meta dynamic_meta[NTL_DYNAMIC_TAG_MAX];
  ntl_tag_atlas_entry dynamic_atlas[NTL_DYNAMIC_TAG_MAX];
  texture *dynamic_atlas_tex;
  VkDescriptorSet dynamic_atlas_ds;
  int fixed_tag_count;
  ntl_tag_meta fixed_meta[NTL_FIXED_TAG_MAX];
  ntl_tag_atlas_entry fixed_atlas[NTL_FIXED_TAG_MAX];
  texture *fixed_atlas_tex;
  VkDescriptorSet fixed_atlas_ds;

  /* A freshly-decoded catalog waiting on its async GPU upload
     (tcontext_async_transfer_busy) before it's safe to swap into the
     fields above. Avoids the old vkWaitForFences(UINT64_MAX) main-thread
     stall on every hourly catalog refresh. */
  bool dynamic_swap_pending;
  texture *dynamic_atlas_tex_pending;
  ntl_tag_meta dynamic_meta_pending[NTL_DYNAMIC_TAG_MAX];
  ntl_tag_atlas_entry dynamic_atlas_pending[NTL_DYNAMIC_TAG_MAX];
  int dynamic_tag_count_pending;
  int private_tag_min_pending;

  bool fixed_swap_pending;
  texture *fixed_atlas_tex_pending;
  ntl_tag_meta fixed_meta_pending[NTL_FIXED_TAG_MAX];
  ntl_tag_atlas_entry fixed_atlas_pending[NTL_FIXED_TAG_MAX];
  int fixed_tag_count_pending;
  int public_tag_max_pending;
  int mapped_tag_count;
  double last_mapping_received;
  char status[192];

  texture *atlas_tex;
  VkDescriptorSet atlas_ds;
} ntl_tags_state;

static ntl_tags_state S;

static void set_status(const char *text) {
  if (!text) text = "";
  strncpy(S.status, text, sizeof S.status - 1);
  S.status[sizeof S.status - 1] = 0;
}

static snake *local_snake(void) {
  if (!S.env || !S.env->usr) return NULL;
  game_data *g = &S.env->usr->gdata;
  return get_snake(g, g->data.snake_id);
}

static const ntl_tag_meta *find_meta(int id) {
  for (int i = 0; i < S.fixed_tag_count; ++i)
    if (S.fixed_meta[i].id == id) return &S.fixed_meta[i];
  for (int i = 0; i < S.dynamic_tag_count; ++i)
    if (S.dynamic_meta[i].id == id) return &S.dynamic_meta[i];
  for (int i = 0; i < NTL_TAG_METADATA_COUNT; ++i)
    if (NTL_TAG_METADATA[i].id == id) return &NTL_TAG_METADATA[i];
  return NULL;
}

static const ntl_tag_atlas_entry *find_atlas_entry(int id) {
  for (int i = 0; i < S.fixed_tag_count; ++i)
    if (S.fixed_atlas[i].id == id) return &S.fixed_atlas[i];
  for (int i = 0; i < S.dynamic_tag_count; ++i)
    if (S.dynamic_atlas[i].id == id) return &S.dynamic_atlas[i];
  for (int i = 0; i < NTL_TAG_ATLAS_ENTRY_COUNT; ++i)
    if (NTL_TAG_ATLAS_ENTRIES[i].id == id) return &NTL_TAG_ATLAS_ENTRIES[i];
  return NULL;
}

static bool is_dynamic_atlas_entry(const ntl_tag_atlas_entry *entry) {
  if (!entry) return false;
  for (int i = 0; i < S.dynamic_tag_count; ++i)
    if (entry == &S.dynamic_atlas[i]) return true;
  return false;
}

static bool is_fixed_atlas_entry(const ntl_tag_atlas_entry *entry) {
  if (!entry) return false;
  for (int i = 0; i < S.fixed_tag_count; ++i)
    if (entry == &S.fixed_atlas[i]) return true;
  return false;
}

static bool ensure_atlas_loaded(tenv *env) {
  if (!env || !env->ctx || !env->usr || !env->usr->r) return false;
  if (S.atlas_tex && S.atlas_ds) return true;

  if (!S.atlas_tex)
    S.atlas_tex = create_mipmap_texture(env->ctx,
                                        "app/res/textures/ntl_tags_atlas.png");
  if (!S.atlas_tex) return false;

  if (!S.atlas_ds) {
    S.atlas_ds = igImplVulkan_AddTexture(
        env->usr->r->linear_sampler, S.atlas_tex->view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!S.atlas_ds) {
      destroy_texture(env->ctx, S.atlas_tex);
      S.atlas_tex = NULL;
      return false;
    }
  }
  return true;
}

static VkDescriptorSet atlas_descriptor(tenv *env,
                                        const ntl_tag_atlas_entry *entry) {
  if (is_fixed_atlas_entry(entry)) return S.fixed_atlas_ds;
  if (is_dynamic_atlas_entry(entry)) return S.dynamic_atlas_ds;
  return ensure_atlas_loaded(env) ? S.atlas_ds : VK_NULL_HANDLE;
}

bool ntl_tags_is_packet_tag(int id) {
  if (id >= 0 && id <= 59) return true;
  int public_max = S.public_tag_max >= 303 ? S.public_tag_max : 367;
  return id >= 200 && id <= public_max;
}

bool ntl_tags_is_protected_tag(int id) {
  /* Public fixed tags grow upward from 304, while private/custom tags grow
     downward from 580. Their live catalogues update these two boundaries. */
  int private_min = S.private_tag_min > 0 ? S.private_tag_min : 374;
  return (id >= 60 && id <= 199) || (id >= private_min && id <= 665);
}

bool ntl_tags_exists(int id) {
  if (id == -1) return true;
  /* Remember packet IDs immediately. If its live image is still downloading,
     the snake switches from the temporary fallback to the image automatically
     as soon as the fixed catalogue atlas is installed. */
  if (ntl_tags_is_packet_tag(id)) return true;
  /* Future protected IDs are accepted and then verified by NTL itself. */
  return ntl_tags_is_protected_tag(id);
}

static void md5_hex(const char *text, char out[33]) {
  unsigned char digest[16];
  mg_md5_ctx ctx;
  mg_md5_init(&ctx);
  mg_md5_update(&ctx, (const unsigned char *)text, strlen(text));
  mg_md5_final(&ctx, digest);
  for (int i = 0; i < 16; ++i) sprintf(out + i * 2, "%02x", digest[i]);
  out[32] = 0;
}

static long json_long_flexible(struct mg_str json, const char *path,
                               long fallback) {
  double value = 0.0;
  if (mg_json_get_num(json, path, &value)) return (long)value;
  char *text = mg_json_get_str(json, path);
  if (!text) return fallback;
  char *end = NULL;
  long result = strtol(text, &end, 10);
  if (!end || *end) result = fallback;
  mg_free(text);
  return result;
}

static bool parse_hex_color(const char *text, uint8_t out[3]) {
  if (!text || !out) return false;
  while (*text == '#' && text[1] == '#') ++text;
  if (*text != '#') return false;
  ++text;
  size_t len = strlen(text);
  unsigned value = 0;
  if (len == 3) {
    if (sscanf(text, "%3x", &value) != 1) return false;
    out[0] = (uint8_t)(((value >> 8) & 15u) * 17u);
    out[1] = (uint8_t)(((value >> 4) & 15u) * 17u);
    out[2] = (uint8_t)((value & 15u) * 17u);
    return true;
  }
  if ((len != 6 && len != 8) || sscanf(text, "%8x", &value) != 1)
    return false;
  if (len == 8) value >>= 8;
  out[0] = (uint8_t)((value >> 16) & 255u);
  out[1] = (uint8_t)((value >> 8) & 255u);
  out[2] = (uint8_t)(value & 255u);
  return true;
}

static void parse_css_color(const char *text, uint8_t out[3],
                            uint8_t fallback_r, uint8_t fallback_g,
                            uint8_t fallback_b) {
  out[0] = fallback_r;
  out[1] = fallback_g;
  out[2] = fallback_b;
  if (!text || !text[0] || parse_hex_color(text, out)) return;

  char name[32];
  size_t n = strlen(text);
  if (n >= sizeof name) n = sizeof name - 1;
  for (size_t i = 0; i < n; ++i)
    name[i] = (char)tolower((unsigned char)text[i]);
  name[n] = 0;
  struct named_color { const char *name; uint8_t r, g, b; };
  static const struct named_color colors[] = {
      {"black", 0, 0, 0}, {"white", 255, 255, 255},
      {"red", 255, 0, 0}, {"blue", 0, 0, 255},
      {"green", 0, 128, 0}, {"lime", 0, 255, 0},
      {"cyan", 0, 255, 255}, {"aqua", 0, 255, 255},
      {"yellow", 255, 255, 0}, {"orange", 255, 165, 0},
      {"pink", 255, 192, 203}, {"hotpink", 255, 105, 180},
      {"purple", 128, 0, 128}, {"violet", 238, 130, 238},
      {"brown", 165, 42, 42}, {"gold", 255, 215, 0},
      {"silver", 192, 192, 192}, {"grey", 128, 128, 128},
      {"gray", 128, 128, 128}, {"lightblue", 173, 216, 230},
      {"darkblue", 0, 0, 139}, {"midnightblue", 25, 25, 112},
      {"slateblue", 106, 90, 205}, {"darkslategray", 47, 79, 79},
      {"darkslategrey", 47, 79, 79}, {"thistle", 216, 191, 216},
      {"cornflowerblue", 100, 149, 237}, {"turquoise", 64, 224, 208},
      {"khaki", 240, 230, 140}, {"wheat", 245, 222, 179},
      {"tan", 210, 180, 140}, {"beige", 245, 245, 220},
      {"aliceblue", 240, 248, 255}, {"gainsboro", 220, 220, 220},
      {"seashell", 255, 245, 238}, {"transparent", 0, 0, 0}};
  for (size_t i = 0; i < sizeof colors / sizeof colors[0]; ++i) {
    if (!strcmp(name, colors[i].name)) {
      out[0] = colors[i].r;
      out[1] = colors[i].g;
      out[2] = colors[i].b;
      return;
    }
  }
}

static unsigned char *decode_data_uri_rgba(const char *src,
                                            int *width, int *height) {
  if (!src || !width || !height) return NULL;
  const char *comma = strchr(src, ',');
  if (!comma || !strstr(src, ";base64")) return NULL;
  const char *base64 = comma + 1;
  size_t base64_len = strlen(base64);
  if (!base64_len || base64_len > NTL_TAG_CATALOG_MAX_BYTES) return NULL;
  size_t cap = base64_len * 3u / 4u + 4u;
  unsigned char *encoded = (unsigned char *)malloc(cap);
  if (!encoded) return NULL;
  size_t encoded_len = mg_base64_decode(base64, base64_len,
                                        (char *)encoded, cap);
  unsigned char *rgba = encoded_len
      ? decode_texture_rgba_from_memory(encoded, encoded_len, width, height)
      : NULL;
  free(encoded);
  return rgba;
}

static void blit_tag_to_atlas(unsigned char *atlas,
                              const unsigned char *src,
                              int src_w, int src_h,
                              int canvas_w, int canvas_h, int slot,
                              ntl_tag_atlas_entry *entry) {
  if (canvas_w < 1) canvas_w = src_w;
  if (canvas_h < 1) canvas_h = src_h;
  int col = slot % NTL_DYNAMIC_ATLAS_COLUMNS;
  int row = slot / NTL_DYNAMIC_ATLAS_COLUMNS;
  int cell_x = col * NTL_DYNAMIC_ATLAS_CELL;
  int cell_y = row * NTL_DYNAMIC_ATLAS_CELL;
  int max_side = NTL_DYNAMIC_ATLAS_CELL - NTL_DYNAMIC_ATLAS_PADDING * 2;
  float scale = GLM_MIN((float)max_side / (float)canvas_w,
                        (float)max_side / (float)canvas_h);
  int dst_w = GLM_MAX(1, (int)lroundf(canvas_w * scale));
  int dst_h = GLM_MAX(1, (int)lroundf(canvas_h * scale));
  int dst_x = cell_x + (NTL_DYNAMIC_ATLAS_CELL - dst_w) / 2;
  int dst_y = cell_y + (NTL_DYNAMIC_ATLAS_CELL - dst_h) / 2;

  for (int y = 0; y < dst_h; ++y) {
    int canvas_y = GLM_MIN(canvas_h - 1, y * canvas_h / dst_h);
    int sy = canvas_y - 21;
    if (sy < 0 || sy >= src_h) continue;
    for (int x = 0; x < dst_w; ++x) {
      int canvas_x = GLM_MIN(canvas_w - 1, x * canvas_w / dst_w);
      int sx = canvas_x - 21;
      if (sx < 0 || sx >= src_w) continue;
      size_t src_off = ((size_t)sy * (size_t)src_w + (size_t)sx) * 4u;
      size_t dst_off = ((size_t)(dst_y + y) * NTL_DYNAMIC_ATLAS_SIZE +
                        (size_t)(dst_x + x)) * 4u;
      memcpy(atlas + dst_off, src + src_off, 4u);
    }
  }

  entry->u0 = (float)dst_x / NTL_DYNAMIC_ATLAS_SIZE;
  entry->v0 = (float)dst_y / NTL_DYNAMIC_ATLAS_SIZE;
  entry->u1 = (float)(dst_x + dst_w) / NTL_DYNAMIC_ATLAS_SIZE;
  entry->v1 = (float)(dst_y + dst_h) / NTL_DYNAMIC_ATLAS_SIZE;
}

static bool install_dynamic_catalog(struct mg_str json) {
  if (!S.env || !S.env->ctx || !S.env->usr || !S.env->usr->r ||
      json.len == 0 || json.len > NTL_TAG_CATALOG_MAX_BYTES)
    return false;

  size_t atlas_bytes = (size_t)NTL_DYNAMIC_ATLAS_SIZE *
                       NTL_DYNAMIC_ATLAS_SIZE * 4u;
  unsigned char *pixels = (unsigned char *)calloc(1, atlas_bytes);
  if (!pixels) return false;
  ntl_tag_meta parsed_meta[NTL_DYNAMIC_TAG_MAX];
  ntl_tag_atlas_entry parsed_atlas[NTL_DYNAMIC_TAG_MAX];
  int parsed_count = 0;
  int source_count = 0;

  for (int source_index = 0; source_index < NTL_DYNAMIC_TAG_MAX;
       ++source_index) {
    char path[80];
    snprintf(path, sizeof path, "$.d[%d].src", source_index);
    char *src = mg_json_get_str(json, path);
    if (!src) break;
    ++source_count;

    int image_w = 0, image_h = 0;
    unsigned char *rgba = decode_data_uri_rgba(src, &image_w, &image_h);
    mg_free(src);
    if (!rgba || image_w <= 0 || image_h <= 0) {
      free_texture_rgba(rgba);
      continue;
    }

    int id = 580 - source_index;
    ntl_tag_meta meta;
    memset(&meta, 0, sizeof meta);
    meta.id = id;
    snprintf(path, sizeof path, "$.d[%d].w", source_index);
    long display_w = json_long_flexible(json, path, image_w);
    snprintf(path, sizeof path, "$.d[%d].h", source_index);
    long display_h = json_long_flexible(json, path, image_h);
    snprintf(path, sizeof path, "$.d[%d].bx", source_index);
    long bx = json_long_flexible(json, path, -32);
    snprintf(path, sizeof path, "$.d[%d].by", source_index);
    long by = json_long_flexible(json, path, -70);
    meta.width = (int16_t)GLM_MAX(1, GLM_MIN(32767, display_w));
    meta.height = (int16_t)GLM_MAX(1, GLM_MIN(32767, display_h));
    meta.offset_x = (float)bx;
    meta.offset_y = (float)by;
    snprintf(path, sizeof path, "$.d[%d].c1", source_index);
    char *c1 = mg_json_get_str(json, path);
    snprintf(path, sizeof path, "$.d[%d].c2", source_index);
    char *c2 = mg_json_get_str(json, path);
    parse_css_color(c1, meta.c1, 0, 180, 210);
    parse_css_color(c2, meta.c2, 120, 225, 255);
    if (c1) mg_free(c1);
    if (c2) mg_free(c2);

    ntl_tag_atlas_entry atlas_entry;
    memset(&atlas_entry, 0, sizeof atlas_entry);
    atlas_entry.id = id;
    blit_tag_to_atlas(pixels, rgba, image_w, image_h,
                      meta.width, meta.height, parsed_count, &atlas_entry);
    free_texture_rgba(rgba);
    parsed_meta[parsed_count] = meta;
    parsed_atlas[parsed_count] = atlas_entry;
    ++parsed_count;
  }

  if (parsed_count == 0) {
    free(pixels);
    return false;
  }
  texture *new_tex = create_mipmap_texture_from_rgba_async(
      S.env->ctx, pixels, NTL_DYNAMIC_ATLAS_SIZE, NTL_DYNAMIC_ATLAS_SIZE);
  free(pixels);
  if (!new_tex) return false;

  /* Don't touch the live S.dynamic_atlas_* fields yet — the GPU upload
     is still in flight. ntl_tags_update() finalizes this (descriptor set
     + swap-in) once tcontext_async_transfer_busy() clears. */
  memcpy(S.dynamic_meta_pending, parsed_meta,
         sizeof parsed_meta[0] * (size_t)parsed_count);
  memcpy(S.dynamic_atlas_pending, parsed_atlas,
         sizeof parsed_atlas[0] * (size_t)parsed_count);
  S.dynamic_tag_count_pending = parsed_count;
  S.private_tag_min_pending = GLM_MAX(304, 581 - source_count);
  S.dynamic_atlas_tex_pending = new_tex;
  S.dynamic_swap_pending = true;
  return true;
}

static bool install_fixed_catalog(struct mg_str json) {
  if (!S.env || !S.env->ctx || !S.env->usr || !S.env->usr->r ||
      json.len == 0 || json.len > NTL_TAG_CATALOG_MAX_BYTES)
    return false;

  size_t atlas_bytes = (size_t)NTL_DYNAMIC_ATLAS_SIZE *
                       NTL_DYNAMIC_ATLAS_SIZE * 4u;
  unsigned char *pixels = (unsigned char *)calloc(1, atlas_bytes);
  if (!pixels) return false;
  ntl_tag_meta parsed_meta[NTL_FIXED_TAG_MAX];
  ntl_tag_atlas_entry parsed_atlas[NTL_FIXED_TAG_MAX];
  int parsed_count = 0;
  int source_count = 0;

  for (int source_index = 0; source_index < NTL_FIXED_TAG_MAX;
       ++source_index) {
    char path[96];
    snprintf(path, sizeof path, "$.d[%d].fst_src", source_index);
    char *src = mg_json_get_str(json, path);
    if (!src) break;
    ++source_count;

    int image_w = 0, image_h = 0;
    unsigned char *rgba = decode_data_uri_rgba(src, &image_w, &image_h);
    mg_free(src);
    if (!rgba || image_w <= 0 || image_h <= 0) {
      free_texture_rgba(rgba);
      continue;
    }

    ntl_tag_meta meta;
    memset(&meta, 0, sizeof meta);
    meta.id = 304 + source_index;
    snprintf(path, sizeof path, "$.d[%d].fst_blbw", source_index);
    long display_w = json_long_flexible(json, path, image_w);
    snprintf(path, sizeof path, "$.d[%d].fst_blbh", source_index);
    long display_h = json_long_flexible(json, path, image_h);
    snprintf(path, sizeof path, "$.d[%d].fst_blbx", source_index);
    long bx = json_long_flexible(json, path, -32);
    snprintf(path, sizeof path, "$.d[%d].fst_blby", source_index);
    long by = json_long_flexible(json, path, -70);
    meta.width = (int16_t)GLM_MAX(1, GLM_MIN(32767, display_w));
    meta.height = (int16_t)GLM_MAX(1, GLM_MIN(32767, display_h));
    meta.offset_x = (float)bx;
    meta.offset_y = (float)by;
    snprintf(path, sizeof path, "$.d[%d].fst_atc1", source_index);
    char *c1 = mg_json_get_str(json, path);
    snprintf(path, sizeof path, "$.d[%d].fst_atc2", source_index);
    char *c2 = mg_json_get_str(json, path);
    parse_css_color(c1, meta.c1, 0, 180, 210);
    parse_css_color(c2, meta.c2, 120, 225, 255);
    if (c1) mg_free(c1);
    if (c2) mg_free(c2);

    ntl_tag_atlas_entry atlas_entry;
    memset(&atlas_entry, 0, sizeof atlas_entry);
    atlas_entry.id = meta.id;
    blit_tag_to_atlas(pixels, rgba, image_w, image_h,
                      meta.width, meta.height, parsed_count, &atlas_entry);
    free_texture_rgba(rgba);
    parsed_meta[parsed_count] = meta;
    parsed_atlas[parsed_count] = atlas_entry;
    ++parsed_count;
  }

  if (parsed_count == 0) {
    free(pixels);
    return false;
  }
  texture *new_tex = create_mipmap_texture_from_rgba_async(
      S.env->ctx, pixels, NTL_DYNAMIC_ATLAS_SIZE, NTL_DYNAMIC_ATLAS_SIZE);
  free(pixels);
  if (!new_tex) return false;

  memcpy(S.fixed_meta_pending, parsed_meta,
         sizeof parsed_meta[0] * (size_t)parsed_count);
  memcpy(S.fixed_atlas_pending, parsed_atlas,
         sizeof parsed_atlas[0] * (size_t)parsed_count);
  S.fixed_tag_count_pending = parsed_count;
  S.public_tag_max_pending = 303 + source_count;
  S.fixed_atlas_tex_pending = new_tex;
  S.fixed_swap_pending = true;
  return true;
}

static void set_local_visual_tag(int id) {
  snake *me = local_snake();
  if (!me) return;
  me->ntl_tag_id = id >= 0 ? id : me->ntl_packet_tag_id;
}

static void reset_ws_handshake(void) {
  S.handshake_sent = false;
  S.last_position_send = 0;
}

static void restart_ws_registration(void) {
  reset_ws_handshake();
  /* NTL sends the full tag registration only as the first message on a new
     mapping connection. Reconnect after a local selection change so the
     server cannot mistake the full registration array for a position update. */
  if (S.ws)
    S.ws->is_closing = 1;
  else
    S.next_connect = 0;
}

static void commit_tag(int id, const char *hash) {
  if (!S.env || !S.env->usr) return;
  user_settings *us = &S.env->usr->usrs;
  us->ntl_tag_id = id;
  if (hash) {
    strncpy(us->ntl_tag_password_md5, hash,
            sizeof us->ntl_tag_password_md5 - 1);
    us->ntl_tag_password_md5[sizeof us->ntl_tag_password_md5 - 1] = 0;
  } else {
    us->ntl_tag_password_md5[0] = 0;
  }
  save_user_settings(us);
  S.preview_tag_id = id;
  set_local_visual_tag(id);
  restart_ws_registration();
}

static void trim_copy(char *dst, size_t dst_size, const char *src, size_t len) {
  while (len && isspace((unsigned char)*src)) {
    ++src;
    --len;
  }
  while (len && isspace((unsigned char)src[len - 1])) --len;
  if (len >= dst_size) len = dst_size - 1;
  memcpy(dst, src, len);
  dst[len] = 0;
}

static void auth_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != S.auth) return;
  if (ev == MG_EV_CONNECT) {
    struct mg_tls_opts tls = {
        .name = mg_str(NTL_TAG_HOST), .skip_verification = 1};
    mg_tls_init(c, &tls);

    /* Match NTL's interactive `!tag id pass` command exactly: that command
       takes the plaintext password, not its MD5 digest (the digest is only
       used afterwards, as the "sa" identity on the position-mapping
       websocket). Sending the hash here made every private-tag
       authorization request fail. The password can contain characters
       that are unsafe in a URL, so percent-encode it first. */
    char pass_enc[sizeof S.pending_password * 3 + 8];
    mg_url_encode(S.pending_password, strlen(S.pending_password),
                 pass_enc, sizeof pass_enc);
    mg_printf(c,
              "GET " NTL_TAG_AUTH_PATH "?id=%d&pass=%s HTTP/1.1\r\n"
              "Host: " NTL_TAG_HOST "\r\n"
              "User-Agent: Mozilla/5.0 (Linux; Android) AppleWebKit/537.36 "
              "Chrome/131.0 Mobile Safari/537.36\r\n"
              "Origin: https://slither.io\r\n"
              "Referer: https://slither.io/\r\n"
              "Accept: */*\r\n"
              "Connection: close\r\n\r\n",
              S.pending_tag_id, pass_enc);
    /* Don't keep the plaintext around any longer than needed to send it. */
    memset(S.pending_password, 0, sizeof S.pending_password);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    char body[128];
    trim_copy(body, sizeof body, hm->body.buf, hm->body.len);
    int status = mg_http_status(hm);
    bool ok = status >= 200 && status < 300 && !strcmp(body, "ok");
    bool rejected = status >= 200 && status < 300 && !ok;
    if (ok) {
      int id = S.pending_tag_id;
      commit_tag(id, S.pending_hash);
      char msg[176];
      snprintf(msg, sizeof msg,
               "Private NTL tag %d authorized and published.",
               id);
      set_status(msg);
      ntl_team_system_message(msg);
    } else {
      char msg[224];
      if (rejected)
        snprintf(msg, sizeof msg,
                 "NTL rejected the password for private tag %d. Ask the tag owner for the current password.",
                 S.pending_tag_id);
      else if (body[0])
        snprintf(msg, sizeof msg,
                 "NTL private-tag authorization failed (HTTP %d): %.120s",
                 status, body);
      else
        snprintf(msg, sizeof msg,
                 "NTL private-tag authorization failed (HTTP %d).", status);
      set_status(msg);
      ntl_team_system_message(msg);
    }
    S.pending_tag_id = -1;
    S.auth_started = 0.0;
    S.auth = NULL;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (S.auth == c) {
      S.auth = NULL;
      S.pending_tag_id = -1;
      S.auth_started = 0.0;
      set_status("NTL private-tag server connection failed.");
      ntl_team_system_message(S.status);
    }
  }
}

static void catalog_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != S.catalog) return;
  if (ev == MG_EV_CONNECT) {
    struct mg_tls_opts tls = {
        .name = mg_str(NTL_TAG_HOST), .skip_verification = 1};
    mg_tls_init(c, &tls);
    mg_printf(c,
              "GET " NTL_TAG_CATALOG_PATH "?v=1 HTTP/1.1\r\n"
              "Host: " NTL_TAG_HOST "\r\n"
              "User-Agent: Mozilla/5.0 (Linux; Android) AppleWebKit/537.36 "
              "Chrome/131.0 Mobile Safari/537.36\r\n"
              "Origin: https://slither.io\r\n"
              "Referer: https://slither.io/\r\n"
              "Accept: application/json,*/*\r\n"
              "Connection: close\r\n\r\n");
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    int status = mg_http_status(hm);
    double now = mg_millis() / 1000.0;
    if (status >= 200 && status < 300 && hm->body.len == 0) {
      S.next_catalog_sync = now + NTL_TAG_CATALOG_REFRESH_SECONDS;
    } else if (status >= 200 && status < 300 &&
               hm->body.len <= NTL_TAG_CATALOG_MAX_BYTES &&
               install_dynamic_catalog(hm->body)) {
      long version = mg_json_get_long(hm->body, "$.v", S.catalog_version);
      if (version > 0) S.catalog_version = version;
      S.next_catalog_sync = now + NTL_TAG_CATALOG_REFRESH_SECONDS;
      char msg[192];
      snprintf(msg, sizeof msg,
               "Latest NTL private tags synced: %d tags (version %ld).",
               S.dynamic_tag_count, S.catalog_version);
      set_status(msg);
      ntl_team_system_message(msg);
    } else {
      S.next_catalog_sync = now + NTL_TAG_CATALOG_RETRY_SECONDS;
      char msg[176];
      snprintf(msg, sizeof msg,
               "NTL private-tag catalog sync failed (HTTP %d). Retrying.",
               status);
      set_status(msg);
    }
    S.catalog = NULL;
    S.catalog_started = 0.0;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (S.catalog == c) {
      S.catalog = NULL;
      S.catalog_started = 0.0;
      S.next_catalog_sync = mg_millis() / 1000.0 +
                            NTL_TAG_CATALOG_RETRY_SECONDS;
      if (S.dynamic_tag_count == 0)
        set_status("NTL private-tag catalog connection failed. Retrying.");
    }
  }
}

static void fixed_catalog_cb(struct mg_connection *c, int ev,
                             void *ev_data) {
  if (c != S.fixed_catalog) return;
  if (ev == MG_EV_CONNECT) {
    struct mg_tls_opts tls = {
        .name = mg_str(NTL_TAG_HOST), .skip_verification = 1};
    mg_tls_init(c, &tls);
    /* Vlither does not persist downloaded image bytes, so request the complete
       update set on every refresh instead of an incremental delta. */
    mg_printf(c,
              "GET " NTL_TAG_FIXED_CATALOG_PATH "?v=1 HTTP/1.1\r\n"
              "Host: " NTL_TAG_HOST "\r\n"
              "User-Agent: Mozilla/5.0 (Linux; Android) AppleWebKit/537.36 "
              "Chrome/131.0 Mobile Safari/537.36\r\n"
              "Origin: https://slither.io\r\n"
              "Referer: https://slither.io/\r\n"
              "Accept: application/json,*/*\r\n"
              "Connection: close\r\n\r\n");
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    int status = mg_http_status(hm);
    double now = mg_millis() / 1000.0;
    if (status >= 200 && status < 300 && hm->body.len == 0) {
      S.next_fixed_catalog_sync = now + NTL_TAG_CATALOG_REFRESH_SECONDS;
    } else if (status >= 200 && status < 300 &&
               hm->body.len <= NTL_TAG_CATALOG_MAX_BYTES &&
               install_fixed_catalog(hm->body)) {
      long version = mg_json_get_long(
          hm->body, "$.v", S.fixed_catalog_version);
      if (version > 0) S.fixed_catalog_version = version;
      S.next_fixed_catalog_sync = now + NTL_TAG_CATALOG_REFRESH_SECONDS;
      char msg[192];
      snprintf(msg, sizeof msg,
               "Latest NTL public tags synced: %d tags (version %ld).",
               S.fixed_tag_count, S.fixed_catalog_version);
      set_status(msg);
    } else {
      S.next_fixed_catalog_sync = now + NTL_TAG_CATALOG_RETRY_SECONDS;
      char msg[176];
      snprintf(msg, sizeof msg,
               "NTL public-tag catalogue sync failed (HTTP %d). Retrying.",
               status);
      set_status(msg);
    }
    S.fixed_catalog = NULL;
    S.fixed_catalog_started = 0.0;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (S.fixed_catalog == c) {
      S.fixed_catalog = NULL;
      S.fixed_catalog_started = 0.0;
      S.next_fixed_catalog_sync = mg_millis() / 1000.0 +
                                  NTL_TAG_CATALOG_RETRY_SECONDS;
      if (S.fixed_tag_count == 0)
        set_status("NTL public-tag catalogue connection failed. Retrying.");
    }
  }
}

static void begin_catalog_sync(void) {
  if (S.catalog || !S.env || !S.env->ctx || !S.env->usr || !S.env->usr->r)
    return;
  S.catalog = mg_http_connect(&S.mgr, "https://ntl-slither.com",
                              catalog_cb, NULL);
  S.catalog_started = mg_millis() / 1000.0;
  if (!S.catalog) {
    S.catalog_started = 0.0;
    S.next_catalog_sync = mg_millis() / 1000.0 +
                          NTL_TAG_CATALOG_RETRY_SECONDS;
  }
}

static void begin_fixed_catalog_sync(void) {
  if (S.fixed_catalog || !S.env || !S.env->ctx || !S.env->usr ||
      !S.env->usr->r)
    return;
  S.fixed_catalog = mg_http_connect(&S.mgr, "https://ntl-slither.com",
                                    fixed_catalog_cb, NULL);
  S.fixed_catalog_started = mg_millis() / 1000.0;
  if (!S.fixed_catalog) {
    S.fixed_catalog_started = 0.0;
    S.next_fixed_catalog_sync = mg_millis() / 1000.0 +
                                NTL_TAG_CATALOG_RETRY_SECONDS;
  }
}

static void json_escape(char *dst, size_t dst_size, const char *src) {
  size_t n = 0;
  if (!dst_size) return;
  for (; *src && n + 1 < dst_size; ++src) {
    unsigned char ch = (unsigned char)*src;
    if ((ch == '"' || ch == '\\') && n + 2 < dst_size) {
      dst[n++] = '\\';
      dst[n++] = (char)ch;
    } else if (ch >= 32) {
      dst[n++] = (char)ch;
    }
  }
  dst[n] = 0;
}

static void apply_mapping(const uint8_t *data, size_t len) {
  if (!S.env || !S.env->usr) return;
  game_data *g = &S.env->usr->gdata;
  user_settings *us = &S.env->usr->usrs;
  int count = tdarray_length(g->data.snakes);
  int mapped_count = 0;

  for (int i = 0; i < count; ++i) {
    snake *o = &g->data.snakes[i];
    bool found = false;
    int mapped = -1;
    for (size_t j = 0; j + 3 < len; j += 4) {
      uint16_t ntl_id = (uint16_t)((data[j] << 8) | data[j + 1]);
      if (ntl_id == o->ntl_id) {
        uint16_t raw = (uint16_t)((data[j + 2] << 8) | data[j + 3]);
        mapped = raw == 65535 ? -1 : (int)raw;
        found = true;
        break;
      }
    }

    bool is_local = o->id == g->data.snake_id;
    if (found && mapped >= 0 && mapped <= 666) {
      o->ntl_tag_id = mapped;
      mapped_count++;
    } else if (is_local && ntl_tags_is_protected_tag(us->ntl_tag_id) &&
               us->ntl_tag_password_md5[0]) {
      o->ntl_tag_id = us->ntl_tag_id;
    } else {
      o->ntl_tag_id = o->ntl_packet_tag_id;
    }
  }
  S.mapped_tag_count = mapped_count;
  S.last_mapping_received = mg_millis() / 1000.0;
}

static void tag_ws_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != S.ws) return;
  if (ev == MG_EV_WS_OPEN) {
    S.ws_open = true;
    reset_ws_handshake();
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    if (wm->data.len >= 4 && wm->data.len % 4 == 0)
      apply_mapping((const uint8_t *)wm->data.buf, wm->data.len);
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (S.ws == c) {
      S.ws = NULL;
      S.ws_open = false;
      reset_ws_handshake();
      S.next_connect = mg_millis() / 1000.0 + NTL_TAG_RECONNECT_SECONDS;
    }
  }
}

static void send_tag_handshake(snake *me) {
  if (!S.ws || !S.ws_open || !S.env || !S.env->usr || !me) return;
  user_settings *us = &S.env->usr->usrs;

  /* NTL's `sa` identity is the stable eight-hex client prefix followed by
     the visible nickname. The mapping server uses this together with server
     and secondary snake ID; omitting the prefix prevents private mappings. */
  char raw_nick[sizeof us->ntl_client_id + MAX_NICKNAME_LEN + 2];
  snprintf(raw_nick, sizeof raw_nick, "%s%s", us->ntl_client_id,
           us->nickname[0] ? us->nickname : "Vlither");
  char nick[sizeof raw_nick * 2];
  char server[(MAX_SERVER_IP_LEN + 1) * 2];
  json_escape(nick, sizeof nick, raw_nick);
  json_escape(server, sizeof server, us->server_address);

  int custom_id = -1;
  const char *hash = "";
  if (ntl_tags_is_protected_tag(us->ntl_tag_id) &&
      us->ntl_tag_password_md5[0]) {
    custom_id = us->ntl_tag_id;
    hash = us->ntl_tag_password_md5;
  }

  char json[512];
  int n = snprintf(json, sizeof json,
                   "[\"%s\",\"%s\",\"%s\",%d,0,%d,%d,%u]",
                   nick, server, hash, custom_id,
                   (int)lroundf(me->xx + me->fx),
                   (int)lroundf(me->yy + me->fy),
                   (unsigned)me->ntl_id);
  if (n > 0 && n < (int)sizeof json) {
    mg_ws_send(S.ws, json, (size_t)n, WEBSOCKET_OP_TEXT);
    S.handshake_sent = true;
    S.last_position_send = mg_millis() / 1000.0;
  }
}

static void send_position(snake *me) {
  if (!S.ws || !S.ws_open || !me) return;
  char json[96];
  int n = snprintf(json, sizeof json, "[%d,%d]",
                   (int)lroundf(me->xx + me->fx),
                   (int)lroundf(me->yy + me->fy));
  if (n > 0 && n < (int)sizeof json)
    mg_ws_send(S.ws, json, (size_t)n, WEBSOCKET_OP_TEXT);
}

void ntl_tags_init(tenv *env) {
  memset(&S, 0, sizeof S);
  S.env = env;
  S.pending_tag_id = -1;
  S.preview_tag_id = (env && env->usr) ? env->usr->usrs.ntl_tag_id : -1;
  S.catalog_version = 1;
  S.fixed_catalog_version = 1;
  S.public_tag_max = 367;
  S.private_tag_min = 374;
  S.next_catalog_sync = 0.0;
  S.next_fixed_catalog_sync = 0.0;
  set_status("Syncing the latest NTL public and private tags...");
  mg_mgr_init(&S.mgr);
  S.ready = true;
}

/* Called once per frame. Cheap no-op unless a catalog refresh just
   finished decoding and its async GPU upload has landed. */
static void finalize_pending_catalog_swaps(tenv *env) {
  if (!env || !env->ctx) return;
  if (tcontext_async_transfer_busy(env->ctx)) return;  /* still uploading */

  if (S.dynamic_swap_pending) {
    VkDescriptorSet new_ds = igImplVulkan_AddTexture(
        env->usr->r->linear_sampler, S.dynamic_atlas_tex_pending->view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (new_ds) {
      if (S.dynamic_atlas_ds) igImplVulkan_RemoveTexture(S.dynamic_atlas_ds);
      if (S.dynamic_atlas_tex) destroy_texture(env->ctx, S.dynamic_atlas_tex);
      memcpy(S.dynamic_meta, S.dynamic_meta_pending,
             sizeof S.dynamic_meta_pending[0] *
                 (size_t)S.dynamic_tag_count_pending);
      memcpy(S.dynamic_atlas, S.dynamic_atlas_pending,
             sizeof S.dynamic_atlas_pending[0] *
                 (size_t)S.dynamic_tag_count_pending);
      S.dynamic_tag_count = S.dynamic_tag_count_pending;
      S.private_tag_min = S.private_tag_min_pending;
      S.dynamic_atlas_tex = S.dynamic_atlas_tex_pending;
      S.dynamic_atlas_ds = new_ds;
    } else {
      destroy_texture(env->ctx, S.dynamic_atlas_tex_pending);
    }
    S.dynamic_atlas_tex_pending = NULL;
    S.dynamic_swap_pending = false;
  }

  if (S.fixed_swap_pending) {
    VkDescriptorSet new_ds = igImplVulkan_AddTexture(
        env->usr->r->linear_sampler, S.fixed_atlas_tex_pending->view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (new_ds) {
      if (S.fixed_atlas_ds) igImplVulkan_RemoveTexture(S.fixed_atlas_ds);
      if (S.fixed_atlas_tex) destroy_texture(env->ctx, S.fixed_atlas_tex);
      memcpy(S.fixed_meta, S.fixed_meta_pending,
             sizeof S.fixed_meta_pending[0] *
                 (size_t)S.fixed_tag_count_pending);
      memcpy(S.fixed_atlas, S.fixed_atlas_pending,
             sizeof S.fixed_atlas_pending[0] *
                 (size_t)S.fixed_tag_count_pending);
      S.fixed_tag_count = S.fixed_tag_count_pending;
      S.public_tag_max = S.public_tag_max_pending;
      S.fixed_atlas_tex = S.fixed_atlas_tex_pending;
      S.fixed_atlas_ds = new_ds;
    } else {
      destroy_texture(env->ctx, S.fixed_atlas_tex_pending);
    }
    S.fixed_atlas_tex_pending = NULL;
    S.fixed_swap_pending = false;
  }
}

void ntl_tags_update(tenv *env) {
  if (!S.ready) return;
  if (env) S.env = env;
  mg_mgr_poll(&S.mgr, 0);
  if (S.env && S.env->ctx) tcontext_poll_async_transfer(S.env->ctx);
  finalize_pending_catalog_swaps(S.env);

  double now = mg_millis() / 1000.0;
  if (S.auth && S.auth_started > 0.0 && now - S.auth_started > 12.0) {
    S.auth->is_closing = 1;
    S.auth = NULL;
    S.pending_tag_id = -1;
    set_status("NTL private-tag authorization timed out.");
    ntl_team_system_message(S.status);
  }
  if (S.catalog && S.catalog_started > 0.0 &&
      now - S.catalog_started > NTL_TAG_CATALOG_TIMEOUT_SECONDS) {
    S.catalog->is_closing = 1;
    S.catalog = NULL;
    S.catalog_started = 0.0;
    S.next_catalog_sync = now + NTL_TAG_CATALOG_RETRY_SECONDS;
    if (S.dynamic_tag_count == 0)
      set_status("NTL private-tag catalog timed out. Retrying.");
  }
  if (S.fixed_catalog && S.fixed_catalog_started > 0.0 &&
      now - S.fixed_catalog_started > NTL_TAG_CATALOG_TIMEOUT_SECONDS) {
    S.fixed_catalog->is_closing = 1;
    S.fixed_catalog = NULL;
    S.fixed_catalog_started = 0.0;
    S.next_fixed_catalog_sync = now + NTL_TAG_CATALOG_RETRY_SECONDS;
    if (S.fixed_tag_count == 0)
      set_status("NTL public-tag catalogue timed out. Retrying.");
  }

  if (!S.env || !S.env->usr) return;
  game_data *g = &S.env->usr->gdata;
  user_settings *us = &S.env->usr->usrs;
  snake *me = local_snake();
  bool in_game = me && g->conn == CONNECTED && g->curr_screen == PLAYING &&
                 !g->preview_active;
  /* Heavy catalogue HTTP (up to multi‑MB) competes with the live Slither
     WebSocket on mobile radios. 4.6 did not do live catalogue pulls during
     play; keep tag WS/positions, but only fetch catalogues in menu/lobby. */
  bool allow_catalog = g->curr_screen != PLAYING || g->preview_active ||
                       g->conn != CONNECTED;
  if (!allow_catalog) {
    if (S.catalog) {
      S.catalog->is_closing = 1;
      S.catalog = NULL;
      S.catalog_started = 0.0;
      /* Retry after the match instead of immediately. */
      if (S.next_catalog_sync < now + 30.0)
        S.next_catalog_sync = now + 30.0;
    }
    if (S.fixed_catalog) {
      S.fixed_catalog->is_closing = 1;
      S.fixed_catalog = NULL;
      S.fixed_catalog_started = 0.0;
      if (S.next_fixed_catalog_sync < now + 30.0)
        S.next_fixed_catalog_sync = now + 30.0;
    }
  } else {
    if (!S.catalog && now >= S.next_catalog_sync) begin_catalog_sync();
    if (!S.fixed_catalog && now >= S.next_fixed_catalog_sync)
      begin_fixed_catalog_sync();
  }

  /* NTL also publishes each team member's active `tg` value through its
     normal presence endpoint. Use that as a second, official compatibility
     path in addition to the tag WebSocket. This is especially important on
     mobile networks that block port 9000. */
  if (in_game) {
    int snake_count = tdarray_length(g->data.snakes);
    for (int i = 0; i < snake_count; ++i) {
      snake *o = &g->data.snakes[i];
      int team_tag = ntl_team_tag_for_snake(o->ntl_id, us->server_address);
      if (team_tag >= 0 && ntl_tags_exists(team_tag))
        o->ntl_tag_id = team_tag;
    }
  }

  if (!in_game) {
    if (S.ws) S.ws->is_closing = 1;
    S.active_server[0] = 0;
    S.active_ntl_id = 0;
    return;
  }

  if (strcmp(S.active_server, us->server_address) != 0 ||
      S.active_ntl_id != me->ntl_id) {
    strncpy(S.active_server, us->server_address, sizeof S.active_server - 1);
    S.active_server[sizeof S.active_server - 1] = 0;
    S.active_ntl_id = me->ntl_id;
    if (S.ws) S.ws->is_closing = 1;
    S.ws = NULL;
    S.ws_open = false;
    reset_ws_handshake();
    S.next_connect = 0;
  }

  if (!S.ws && now >= S.next_connect) {
    S.ws = mg_ws_connect(&S.mgr, NTL_TAG_WS_URL, tag_ws_cb, NULL,
                         "Origin: https://slither.io\r\nUser-Agent: Mozilla/5.0 (Linux; Android) Chrome/131.0 Mobile\r\n");
    if (!S.ws) S.next_connect = now + NTL_TAG_RECONNECT_SECONDS;
  }

  if (S.ws_open && !S.handshake_sent) send_tag_handshake(me);
  if (S.ws_open && S.handshake_sent &&
      now - S.last_position_send >= NTL_TAG_POSITION_SECONDS) {
    send_position(me);
    S.last_position_send = now;
  }

  if (ntl_tags_is_protected_tag(us->ntl_tag_id) &&
      us->ntl_tag_password_md5[0])
    me->ntl_tag_id = us->ntl_tag_id;
}

void ntl_tags_destroy(tenv *env) {
  if (!S.ready) return;
  tcontext *ctx = env ? env->ctx : (S.env ? S.env->ctx : NULL);
  if (ctx && (S.dynamic_atlas_tex_pending || S.fixed_atlas_tex_pending)) {
    tcontext_wait_idle(ctx);  /* shutdown only — safe to block here */
    tcontext_poll_async_transfer(ctx);  /* reclaim the staging buffer */
  }
  if (S.atlas_ds) igImplVulkan_RemoveTexture(S.atlas_ds);
  if (S.dynamic_atlas_ds) igImplVulkan_RemoveTexture(S.dynamic_atlas_ds);
  if (S.fixed_atlas_ds) igImplVulkan_RemoveTexture(S.fixed_atlas_ds);
  if (S.atlas_tex && ctx) destroy_texture(ctx, S.atlas_tex);
  if (S.dynamic_atlas_tex && ctx) destroy_texture(ctx, S.dynamic_atlas_tex);
  if (S.fixed_atlas_tex && ctx) destroy_texture(ctx, S.fixed_atlas_tex);
  if (S.dynamic_atlas_tex_pending && ctx)
    destroy_texture(ctx, S.dynamic_atlas_tex_pending);
  if (S.fixed_atlas_tex_pending && ctx)
    destroy_texture(ctx, S.fixed_atlas_tex_pending);
  mg_mgr_free(&S.mgr);
  memset(&S, 0, sizeof S);
}

bool ntl_tags_select_public(int id) {
  if (!ntl_tags_is_packet_tag(id) || !find_atlas_entry(id) ||
      !S.env || !S.env->usr) {
    set_status("That public tag is not available in this build.");
    return false;
  }
  commit_tag(id, NULL);
  char msg[192];
  snprintf(msg, sizeof msg,
           "Public NTL tag %d selected. Respawn to publish it to other players.",
           id);
  set_status(msg);
  ntl_team_system_message(msg);
  return true;
}

void ntl_tags_disable(void) {
  if (!S.env || !S.env->usr) return;
  commit_tag(-1, NULL);
  set_status("NTL tag disabled.");
  ntl_team_system_message(S.status);
}

bool ntl_tags_request_private(int id, const char *password) {
  if (!S.env || !S.env->usr || !ntl_tags_is_protected_tag(id)) {
    set_status("Enter a valid private NTL tag ID.");
    ntl_team_system_message(S.status);
    return false;
  }
  if (!password || !password[0]) {
    set_status("A password is required for a private tag.");
    ntl_team_system_message(S.status);
    return false;
  }
  if (S.auth) {
    set_status("A private-tag authorization request is already running.");
    ntl_team_system_message(S.status);
    return false;
  }

  md5_hex(password, S.pending_hash);
  strncpy(S.pending_password, password, sizeof S.pending_password - 1);
  S.pending_password[sizeof S.pending_password - 1] = 0;
  S.pending_tag_id = id;
  S.auth = mg_http_connect(&S.mgr, "https://ntl-slither.com", auth_cb, NULL);
  S.auth_started = mg_millis() / 1000.0;
  if (!S.auth) {
    S.pending_tag_id = -1;
    set_status("Could not contact the NTL private-tag server.");
    ntl_team_system_message(S.status);
    return false;
  }
  set_status("Checking the private tag password with NTL...");
  ntl_team_system_message(S.status);
  return true;
}

bool ntl_tags_handle_command(const char *input) {
  if (!input) return false;
  while (isspace((unsigned char)*input)) ++input;

  if (strncmp(input, "!tag", 4) != 0 ||
      (input[4] && !isspace((unsigned char)input[4])))
    return false;

  char copy[512];
  strncpy(copy, input, sizeof copy - 1);
  copy[sizeof copy - 1] = 0;
  char *cmd = strtok(copy, " \t\r\n");
  char *id_text = strtok(NULL, " \t\r\n");
  char *password = strtok(NULL, " \t\r\n");
  (void)cmd;

  if (!id_text) {
    int current = S.env && S.env->usr ? S.env->usr->usrs.ntl_tag_id : -1;
    char msg[160];
    if (current < 0)
      snprintf(msg, sizeof msg,
               "NTL tag: off. Usage: !tag <id> [password], or !tag off.");
    else
      snprintf(msg, sizeof msg,
               "NTL tag: %d. Usage: !tag <id> [password], or !tag off.",
               current);
    ntl_team_system_message(msg);
    return true;
  }

  if (!strcmp(id_text, "off") || !strcmp(id_text, "none") ||
      !strcmp(id_text, "-1")) {
    ntl_tags_disable();
    return true;
  }

  char *end = NULL;
  long parsed = strtol(id_text, &end, 10);
  if (!end || *end || parsed < 0 || parsed > 666 ||
      !ntl_tags_exists((int)parsed)) {
    ntl_team_system_message("Unknown NTL tag ID.");
    return true;
  }
  int id = (int)parsed;

  if (ntl_tags_is_packet_tag(id)) {
    ntl_tags_select_public(id);
    return true;
  }

  ntl_tags_request_private(id, password);
  return true;
}

void ntl_tags_skin_panel(tenv *env) {
  if (!env || !env->usr) return;
  user_settings *us = &env->usr->usrs;
  static int private_id = 60;
  static char private_password[96] = {0};

  igSeparatorText("NTL tags");
  igTextWrapped(
      "NTL Tags, NTL Chat, team positions, and minimap sharing use the "
      "NTL teaming server. When you use these NTL features, Vlither "
      "connects to NTL's server.");
  igSpacing();
  int current = us->ntl_tag_id;
  if (current < 0)
    igText("Selected: Off");
  else
    igText("Selected: %d%s", current,
           ntl_tags_is_packet_tag(current) ? " (public)" : " (private)");
  if (S.preview_tag_id >= 0 && S.preview_tag_id != current)
    igText("Preview: %d", S.preview_tag_id);
  if (S.status[0]) igTextWrapped("%s", S.status);
  if (env->usr->gdata.curr_screen == PLAYING && !env->usr->gdata.preview_active)
    if (S.ws_open)
      igTextDisabled("Private-tag mapping: connected");
    else
      igTextDisabled("Private-tag mapping: reconnecting");
  else
    igTextDisabled("Private-tag mapping connects after you spawn.");

  if (igButton("Disable tag", (ImVec2){0, 0})) ntl_tags_disable();
  igSameLine(0, 8);
  igTextDisabled("Public tags are published after respawn.");

  igSpacing();
  igSeparatorText("Tag editor");
  igTextWrapped("These options change the size of all NTL and Vlither tags.");
  int size_percent = (int)lroundf(us->tag_size_scale * 100.0f);
  igSetNextItemWidth(-1.0f);
  if (igSliderInt("Tag size", &size_percent, 50, 200, "%d%%",
                  ImGuiSliderFlags_AlwaysClamp))
    us->tag_size_scale = size_percent / 100.0f;
  igCheckbox("Change size based on zoom", &us->tag_size_with_zoom);
  if (!us->tag_size_with_zoom)
    igTextDisabled("Tags keep a fixed screen size while zooming.");
  if (igButton("Reset tag size", (ImVec2){0, 0})) {
    us->tag_size_scale = 1.0f;
    us->tag_size_with_zoom = true;
  }

  /* Keep private-tag authorization above the complete image gallery. */
  igSpacing();
  igSeparatorText("Private tag");
  igTextWrapped(
      "Enter the same tag ID and password used by NTL. Latest private-tag "
      "images sync automatically from NTL.");
  if (S.dynamic_tag_count > 0 || S.fixed_tag_count > 0)
    igTextDisabled("Live catalogues: %d public + %d private tags",
                   S.fixed_tag_count, S.dynamic_tag_count);
  else
    igTextDisabled("Live tag catalogues: syncing...");
  igSetNextItemWidth(150.0f);
  if (igInputInt("Tag ID", &private_id, 1, 10,
                 ImGuiInputTextFlags_None)) {
    if (private_id < 0) private_id = 0;
    if (private_id > 666) private_id = 666;
    if (ntl_tags_is_protected_tag(private_id) && find_atlas_entry(private_id))
      S.preview_tag_id = private_id;
  }
  if (private_id < 0) private_id = 0;
  if (private_id > 666) private_id = 666;
  igSetNextItemWidth(-1.0f);
  igInputTextWithHint("##private_tag_password", "Private tag password",
                      private_password, sizeof private_password,
                      ImGuiInputTextFlags_Password, NULL, NULL);
  bool valid_private = ntl_tags_is_protected_tag(private_id);
  igBeginDisabled(!valid_private || S.auth != NULL);
  if (igButton(S.auth ? "Checking..." : "Apply private tag",
               (ImVec2){0, 0}))
    ntl_tags_request_private(private_id, private_password);
  igEndDisabled();
  if (!valid_private)
    igTextColored((ImVec4){1.0f, 0.55f, 0.25f, 1.0f},
                  "This ID is public or outside the private-tag ranges.");

  igSpacing();
  igSeparatorText("Public tags");
  if (!ensure_atlas_loaded(env)) {
    igTextColored((ImVec4){1.0f, 0.35f, 0.35f, 1.0f},
                  "Tag atlas could not be loaded.");
  } else {
    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    ImGuiStyle *style = igGetStyle();
    float tile = 52.0f;
    float cell_w = tile + style->FramePadding.x * 2.0f;
    int columns = (int)((avail.x + style->ItemSpacing.x) /
                        (cell_w + style->ItemSpacing.x));
    if (columns < 3) columns = 3;
    /* A table gives every lower-row image its own clipped hit rectangle. This
       avoids the long SameLine layout losing taps after a large mobile scroll. */
    if (igBeginTable("##ntl_tag_image_grid", columns,
                     ImGuiTableFlags_SizingFixedFit, (ImVec2){0, 0}, 0)) {
      int gallery_count = NTL_TAG_ATLAS_ENTRY_COUNT + S.fixed_tag_count +
                          S.dynamic_tag_count;
      for (int i = 0; i < gallery_count; ++i) {
        const ntl_tag_atlas_entry *a;
        if (i < NTL_TAG_ATLAS_ENTRY_COUNT) {
          a = &NTL_TAG_ATLAS_ENTRIES[i];
        } else if (i < NTL_TAG_ATLAS_ENTRY_COUNT + S.fixed_tag_count) {
          a = &S.fixed_atlas[i - NTL_TAG_ATLAS_ENTRY_COUNT];
        } else {
          a = &S.dynamic_atlas[
              i - NTL_TAG_ATLAS_ENTRY_COUNT - S.fixed_tag_count];
        }
        bool is_public = ntl_tags_is_packet_tag(a->id);
        bool is_private = ntl_tags_is_protected_tag(a->id);
        if (!is_public && !is_private) continue;
        VkDescriptorSet entry_ds = atlas_descriptor(env, a);
        if (!entry_ds) continue;
        ImTextureRef tex = {NULL, (ImTextureID)entry_ds};

        igTableNextColumn();
        bool selected = S.preview_tag_id == a->id;
        char id_label[32];
        snprintf(id_label, sizeof id_label, "##tag_image_%d", a->id);
        igPushStyleColor_Vec4(
            ImGuiCol_Button,
            selected ? (ImVec4){0.20f, 0.62f, 0.30f, 0.85f}
                     : (ImVec4){0.08f, 0.09f, 0.11f, 0.70f});
        igPushStyleColor_Vec4(
            ImGuiCol_ButtonHovered, (ImVec4){0.25f, 0.55f, 0.75f, 0.90f});
        if (igImageButton(id_label, tex, (ImVec2){tile, tile},
                          (ImVec2){a->u0, a->v0},
                          (ImVec2){a->u1, a->v1},
                          (ImVec4){0, 0, 0, 0}, (ImVec4){1, 1, 1, 1})) {
          S.preview_tag_id = a->id;
          if (is_public) {
            ntl_tags_select_public(a->id);
          } else {
            private_id = a->id;
            private_password[0] = 0;
            set_status("Private tag selected for preview. Enter its password above to apply it.");
          }
        }
        igPopStyleColor(2);
      }
      igEndTable();
    }
  }
}

int ntl_tags_preview_id(void) {
  if (S.preview_tag_id >= 0 && find_atlas_entry(S.preview_tag_id))
    return S.preview_tag_id;
  if (S.env && S.env->usr) {
    int selected = S.env->usr->usrs.ntl_tag_id;
    if (selected >= 0 && find_atlas_entry(selected)) return selected;
  }
  return -1;
}

static float tag_display_scale(const user_settings *us, float zoom_scale) {
  float custom = us ? us->tag_size_scale : 1.0f;
  if (!isfinite(custom) || custom < 0.50f || custom > 2.00f) custom = 1.0f;
  float base = (us && !us->tag_size_with_zoom)
                   ? 1.0f
                   : GLM_MAX(0.58f, GLM_MIN(1.75f, zoom_scale));
  return base * custom;
}

static void tag_image_dimensions(const ntl_tag_meta *meta,
                                 const ntl_tag_atlas_entry *atlas,
                                 float body_scale,
                                 float *width, float *height) {
  if (meta) {
    float image_scale = 0.285f * body_scale;
    *width = meta->width * image_scale;
    *height = meta->height * image_scale;
    return;
  }
  float uv_w = atlas ? atlas->u1 - atlas->u0 : 1.0f;
  float uv_h = atlas ? atlas->v1 - atlas->v0 : 1.0f;
  float aspect = uv_h > 0.0001f ? uv_w / uv_h : 1.0f;
  *height = 58.0f * body_scale;
  *width = *height * aspect;
  if (*width > 130.0f * body_scale) *width = 130.0f * body_scale;
}

void ntl_tags_draw_preview(tenv *env, int id, float head_x, float head_y,
                           float angle, float preview_snake_scale) {
  if (!env || !env->usr || id < 0) return;
  const ntl_tag_atlas_entry *atlas = find_atlas_entry(id);
  if (!atlas) return;
  VkDescriptorSet atlas_ds = atlas_descriptor(env, atlas);
  if (!atlas_ds) return;
  const ntl_tag_meta *meta = find_meta(id);
  float custom = env->usr->usrs.tag_size_scale;
  if (!isfinite(custom) || custom < 0.50f || custom > 2.00f) custom = 1.0f;
  float body_scale = GLM_MAX(0.65f, preview_snake_scale) * custom;
  float back_x = -cosf(angle), back_y = -sinf(angle);
  float side_x = -back_y, side_y = back_x;
  float distance = 68.0f * body_scale;
  /* Negative lateral places the preview below the upper snake when it faces
     right, keeping it clear of the screen edge and making the antenna visible. */
  float lateral = -58.0f * body_scale;
  ImVec2 center = {head_x + back_x * distance + side_x * lateral,
                   head_y + back_y * distance + side_y * lateral};
  ImVec2 p0 = {head_x + back_x * 9.0f * body_scale,
               head_y + back_y * 9.0f * body_scale};
  ImVec2 tip = {center.x - back_x * 12.0f * body_scale,
                center.y - back_y * 12.0f * body_scale};
  ImVec2 p1 = {p0.x + back_x * distance * 0.30f,
               p0.y + back_y * distance * 0.30f};
  ImVec2 p2 = {tip.x - back_x * distance * 0.28f,
               tip.y - back_y * distance * 0.28f};
  ImDrawList *dl = igGetWindowDrawList();
  ImVec4 c1 = meta ? (ImVec4){meta->c1[0] / 255.0f,
                              meta->c1[1] / 255.0f,
                              meta->c1[2] / 255.0f, 0.90f}
                   : (ImVec4){0.08f, 0.45f, 0.65f, 0.90f};
  ImVec4 c2 = meta ? (ImVec4){meta->c2[0] / 255.0f,
                              meta->c2[1] / 255.0f,
                              meta->c2[2] / 255.0f, 1.0f}
                   : (ImVec4){0.45f, 0.90f, 1.0f, 1.0f};
  ImDrawList_AddBezierCubic(dl, p0, p1, p2, tip,
                            igColorConvertFloat4ToU32(c1),
                            5.0f * body_scale, 14);
  ImDrawList_AddBezierCubic(dl, p0, p1, p2, tip,
                            igColorConvertFloat4ToU32(c2),
                            2.4f * body_scale, 14);

  float width, height;
  tag_image_dimensions(meta, atlas, body_scale, &width, &height);
  float image_angle = angle + PI * 0.5f;
  float ux = cosf(image_angle), uy = sinf(image_angle);
  float vx = -uy, vy = ux;
  float hw = width * 0.5f, hh = height * 0.5f;
  ImVec2 q1 = {center.x - ux * hw - vx * hh,
               center.y - uy * hw - vy * hh};
  ImVec2 q2 = {center.x + ux * hw - vx * hh,
               center.y + uy * hw - vy * hh};
  ImVec2 q3 = {center.x + ux * hw + vx * hh,
               center.y + uy * hw + vy * hh};
  ImVec2 q4 = {center.x - ux * hw + vx * hh,
               center.y - uy * hw + vy * hh};
  ImTextureRef tex = {NULL, (ImTextureID)atlas_ds};
  ImDrawList_AddImageQuad(dl, tex, q1, q2, q3, q4,
                          (ImVec2){atlas->u0, atlas->v0},
                          (ImVec2){atlas->u1, atlas->v0},
                          (ImVec2){atlas->u1, atlas->v1},
                          (ImVec2){atlas->u0, atlas->v1},
                          IM_COL32(255, 255, 255, 255));
}

static void draw_fallback(ImDrawList *dl, ImVec2 center, int id,
                          float alpha, float scale) {
  char label[32];
  snprintf(label, sizeof label, "TAG %d", id);
  ImVec2 text_size;
  igCalcTextSize(&text_size, label, NULL, false, -1.0f);
  float pad = 7.0f * scale;
  ImVec2 p0 = {center.x - text_size.x * 0.5f - pad,
               center.y - text_size.y * 0.5f - pad * 0.6f};
  ImVec2 p1 = {center.x + text_size.x * 0.5f + pad,
               center.y + text_size.y * 0.5f + pad * 0.6f};
  ImDrawList_AddRectFilled(dl, p0, p1,
                           igColorConvertFloat4ToU32(
                               (ImVec4){0.05f, 0.07f, 0.09f, alpha * 0.9f}),
                           6.0f * scale, 0);
  ImDrawList_AddRect(dl, p0, p1,
                     igColorConvertFloat4ToU32(
                         (ImVec4){0.45f, 0.9f, 1.0f, alpha}),
                     6.0f * scale, 0, 2.0f * scale);
  ImDrawList_AddText_Vec2(
      dl, (ImVec2){center.x - text_size.x * 0.5f,
                   center.y - text_size.y * 0.5f},
      igColorConvertFloat4ToU32((ImVec4){1, 1, 1, alpha}), label, NULL);
}

void ntl_tags_begin_frame(void) {}

void ntl_tags_draw(tenv *env, snake *o, float alpha,
                   float mww2, float mhh2) {
  if (!env || !env->usr || !env->usr->r || !o || o->dead ||
      o->ntl_tag_id < 0 || alpha <= 0.01f)
    return;

  game_data *g = &env->usr->gdata;
  const ntl_tag_meta *meta = find_meta(o->ntl_tag_id);
  float body_scale = tag_display_scale(&env->usr->usrs, o->sc * g->data.gsc);
  float image_scale = 0.285f * body_scale;
  float hx = mww2 + (o->xx + o->fx - g->data.view_xx) * g->data.gsc;
  float hy = mhh2 + (o->yy + o->fy - g->data.view_yy) * g->data.gsc;

  /* The connector begins in the snake's current direction, while the tag
     follows a damped heading. During a turn this creates a soft trailing arc
     instead of rotating the entire tag assembly as one rigid straight piece. */
  float follow_ang = tag_follow_angle(
      &o->ntl_tag_follow_ang, &o->ntl_tag_follow_mtm,
      &o->ntl_tag_follow_ready, o->ang, g->data.ctm, 9.0f);
  float head_back_x = -cosf(o->ang), head_back_y = -sinf(o->ang);
  float tag_back_x = -cosf(follow_ang), tag_back_y = -sinf(follow_ang);
  float tag_side_x = -tag_back_y, tag_side_y = tag_back_x;

  float offset_x = meta ? meta->offset_x + 32.0f : 0.0f;
  float offset_y = meta ? -meta->offset_y : 85.0f;
  float distance = GLM_MAX(58.0f, offset_y * 0.78f) * body_scale;
  float lateral = offset_x * image_scale;
  ImVec2 center = {hx + tag_back_x * distance + tag_side_x * lateral,
                   hy + tag_back_y * distance + tag_side_y * lateral};
  ImVec2 p0 = {hx + head_back_x * 10.0f * body_scale,
               hy + head_back_y * 10.0f * body_scale};
  ImVec2 p1 = {p0.x + head_back_x * distance * 0.32f,
               p0.y + head_back_y * distance * 0.32f};
  ImVec2 tip = {center.x - tag_back_x * 12.0f * body_scale,
                center.y - tag_back_y * 12.0f * body_scale};
  ImVec2 p2 = {tip.x - tag_back_x * distance * 0.30f,
               tip.y - tag_back_y * distance * 0.30f};

  ImDrawList *dl = igGetWindowDrawList();
  ImVec4 c1 = meta ? (ImVec4){meta->c1[0] / 255.0f,
                              meta->c1[1] / 255.0f,
                              meta->c1[2] / 255.0f, alpha * 0.88f}
                   : (ImVec4){0.1f, 0.55f, 0.7f, alpha * 0.88f};
  ImVec4 c2 = meta ? (ImVec4){meta->c2[0] / 255.0f,
                              meta->c2[1] / 255.0f,
                              meta->c2[2] / 255.0f, alpha}
                   : (ImVec4){0.45f, 0.9f, 1.0f, alpha};
  ImDrawList_AddBezierCubic(dl, p0, p1, p2, tip,
                            igColorConvertFloat4ToU32(c1),
                            5.0f * body_scale, 14);
  ImDrawList_AddBezierCubic(dl, p0, p1, p2, tip,
                            igColorConvertFloat4ToU32(c2),
                            2.5f * body_scale, 14);

  const ntl_tag_atlas_entry *atlas = find_atlas_entry(o->ntl_tag_id);
  VkDescriptorSet atlas_ds = atlas_descriptor(env, atlas);
  if (!atlas || !atlas_ds) {
    draw_fallback(dl, center, o->ntl_tag_id, alpha, body_scale);
    return;
  }

  float width, height;
  tag_image_dimensions(meta, atlas, body_scale, &width, &height);
  float angle = atan2f(tip.y - p2.y, tip.x - p2.x) + PI * 0.5f;
  float ux = cosf(angle), uy = sinf(angle);
  float vx = -uy, vy = ux;
  float hw = width * 0.5f, hh = height * 0.5f;
  ImVec2 q1 = {center.x - ux * hw - vx * hh,
               center.y - uy * hw - vy * hh};
  ImVec2 q2 = {center.x + ux * hw - vx * hh,
               center.y + uy * hw - vy * hh};
  ImVec2 q3 = {center.x + ux * hw + vx * hh,
               center.y + uy * hw + vy * hh};
  ImVec2 q4 = {center.x - ux * hw + vx * hh,
               center.y - uy * hw + vy * hh};
  ImTextureRef tex = {NULL, (ImTextureID)atlas_ds};
  ImDrawList_AddImageQuad(dl, tex, q1, q2, q3, q4,
                          (ImVec2){atlas->u0, atlas->v0},
                          (ImVec2){atlas->u1, atlas->v0},
                          (ImVec2){atlas->u1, atlas->v1},
                          (ImVec2){atlas->u0, atlas->v1},
                          igColorConvertFloat4ToU32(
                              (ImVec4){1, 1, 1, alpha}));
}
