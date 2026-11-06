#include "ntl_tags.h"

#include "ntl_tags_data.h"
#include "ntl_tags_atlas_data.h"
#include "ntl_team.h"
#include "tag_follow.h"
#include "tag_cord.h"
#include "ntl_tag_render.h"
#include "../user.h"
#include "../rendering/texture.h"
#include "../cimgui/cimgui_impl.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Node A = the hostname the live NTL extension uses (always current).
// Node B = second relay IP from vlither-main (NTL browsers may land on either
// node, so we register on both). If B is dead it just retries slowly.
#define NTL_TAG_WS_URL_A "ws://ws.ntl-slither.com:9000"
#define NTL_TAG_WS_URL_B "ws://5.2.180.30:9000"
#define NTL_TAG_SALT_WAIT_SECONDS 2.5
#define NTL_TAG_MAP_MAX 1024
#define NTL_TAG_HOST "ntl-slither.com"
#define NTL_TAG_AUTH_PATH "/tags/tag"
#define NTL_TAG_CATALOG_PATH "/tags/cstags"
#define NTL_TAG_FIXED_CATALOG_PATH "/tags/fstags"
#define NTL_TAG_RECONNECT_SECONDS 1.2
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
  struct mg_connection *authz;  // POST /tags/authorizetags.php (per-IP auth)
  double authz_started;
  double authz_next;
  struct mg_connection *catalog;
  struct mg_connection *fixed_catalog;
  bool ws_open;
  bool handshake_sent;
  double next_connect;
  double last_position_send;
  double ws_connected_at;
  // second relay node (own session id so the nodes don't kick each other)
  struct mg_connection *ws2;
  bool ws2_open;
  bool handshake2_sent;
  double next_connect2;
  double last_position_send2;
  double ws2_connected_at;
  char sid2[9];
  // snake id -> tag id map received from the :9000 relay (both nodes)
  struct {
    int id;
    int16_t tag;
  } map[NTL_TAG_MAP_MAX];
  int map_count;
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

// vlither-main's tag table (banner size, colours, anchors). Binary search by id.
static const ntl_tag_def *main_tag_lookup(int id) {
  int lo = 0, hi = NTL_TAG_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (NTL_TAGS[mid].id == id) return &NTL_TAGS[mid];
    if (NTL_TAGS[mid].id < id)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
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

  // same atlas image vlither-main uses: app/res/textures/ntl_tags.png
  static double s_next_try = 0.0;
  double now_s = mg_millis() / 1000.0;
  if (!S.atlas_tex && now_s < s_next_try) return false;  // don't reload every frame
  if (!S.atlas_tex)
    S.atlas_tex = create_mipmap_texture(env->ctx,
                                        "app/res/textures/ntl_tags.png");
  if (!S.atlas_tex) {
    s_next_try = now_s + 5.0;
    return false;
  }

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
  int private_min = S.private_tag_min > 0 ? S.private_tag_min : 374;
  return (id >= 60 && id <= 199) || (id >= private_min && id <= 665);
}

bool ntl_tags_exists(int id) {
  if (id == -1) return true;
  if (ntl_tags_is_packet_tag(id)) return true;
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
  S.handshake2_sent = false;
  S.last_position_send2 = 0;
}

static void restart_ws_registration(void) {
  reset_ws_handshake();
  if (S.ws)
    S.ws->is_closing = 1;
  else
    S.next_connect = 0;
  if (S.ws2)
    S.ws2->is_closing = 1;
  else
    S.next_connect2 = 0;
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

// The :9000 relay only publishes a private tag for an IP that NTL has
// authorized. NTL does GET /tags/tag then POSTs [[id,md5]] to
// /tags/authorizetags.php at login and every 60 s. Without this the relay
// silently drops our private tag. Response: JSON array, 1 = ok, 0 = rejected.
static void authz_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != S.authz) return;
  if (ev == MG_EV_CONNECT) {
    struct mg_tls_opts tls = {
        .name = mg_str(NTL_TAG_HOST), .skip_verification = 1};
    mg_tls_init(c, &tls);
    user_settings *us = &S.env->usr->usrs;
    char body[160];
    int bl = snprintf(body, sizeof body, "[[\"%d\",\"%s\"]]",
                      us->ntl_tag_id, us->ntl_tag_password_md5);
    mg_printf(c,
              "POST /tags/authorizetags.php HTTP/1.1\r\n"
              "Host: " NTL_TAG_HOST "\r\n"
              "User-Agent: Mozilla/5.0 (Linux; Android) AppleWebKit/537.36 "
              "Chrome/131.0 Mobile Safari/537.36\r\n"
              "Origin: https://slither.io\r\n"
              "Referer: https://slither.io/\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "Connection: close\r\n\r\n%s",
              bl, body);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    long v = mg_json_get_long(hm->body, "$[0]", -1);
    double now = mg_millis() / 1000.0;
    if (v == 1) {
      S.authz_next = now + 60.0;
      restart_ws_registration();  // re-register now that our IP is authorized
    } else if (v == 0) {
      S.authz_next = now + 60.0;
      commit_tag(-1, NULL);
      set_status("NTL no longer authorizes this private tag. Tag removed.");
      ntl_team_system_message(S.status);
    } else {
      S.authz_next = now + 10.0;  // unreadable reply -> retry soon
    }
    S.authz = NULL;
    S.authz_started = 0.0;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (S.authz == c) {
      S.authz = NULL;
      S.authz_started = 0.0;
      S.authz_next = mg_millis() / 1000.0 + 10.0;
    }
  }
}

static void start_authorize(void) {
  if (S.authz || !S.env || !S.env->usr) return;
  user_settings *us = &S.env->usr->usrs;
  if (!ntl_tags_is_protected_tag(us->ntl_tag_id) ||
      !us->ntl_tag_password_md5[0])
    return;
  S.authz = mg_http_connect(&S.mgr, "https://ntl-slither.com", authz_cb, NULL);
  S.authz_started = mg_millis() / 1000.0;
  if (!S.authz) S.authz_next = S.authz_started + 10.0;
}

static void auth_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != S.auth) return;
  if (ev == MG_EV_CONNECT) {
    struct mg_tls_opts tls = {
        .name = mg_str(NTL_TAG_HOST), .skip_verification = 1};
    mg_tls_init(c, &tls);

    // NTL sends md5(password) in the URL, never the plain password
    char pass_enc[sizeof S.pending_hash * 3 + 8];
    mg_url_encode(S.pending_hash, strlen(S.pending_hash), pass_enc,
                  sizeof pass_enc);
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
      S.authz_next = 0.0;
      start_authorize();
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

// store the relay's snake-id -> tag-id records (4 bytes each: id, tag).
// like vlither-main, we keep the raw map and resolve per snake every frame
// (resolve_snake_tag) so snakes that appear between pushes get their tag too.
static void apply_mapping(const uint8_t *data, size_t len) {
  int count = 0, mapped_count = 0;
  for (size_t j = 0; j + 3 < len && count < NTL_TAG_MAP_MAX; j += 4) {
    int id = (data[j] << 8) | data[j + 1];
    int tag = (data[j + 2] << 8) | data[j + 3];
    if (tag == 65535) tag = -1;
    S.map[count].id = id;
    S.map[count].tag = (int16_t)tag;
    if (tag >= 0) mapped_count++;
    count++;
  }
  S.map_count = count;
  S.mapped_tag_count = mapped_count;
  S.last_mapping_received = mg_millis() / 1000.0;
}

static int map_lookup_exact(int id) {
  for (int i = 0; i < S.map_count; i++)
    if (S.map[i].id == id && S.map[i].tag >= 0 && S.map[i].tag <= 666)
      return S.map[i].tag;
  return -1;
}

// NTL browser players register under their SALTED ntlid = (salt<<10)|(rawid&1023).
// the low 10 bits are the raw id we already see, so match on those to recover a
// tag even when we don't know the salt. (0 = unset id, skip to avoid matching all.)
static int map_lookup_low10(int id) {
  if ((id & 1023) == 0) return -1;
  for (int i = 0; i < S.map_count; i++)
    if ((S.map[i].id & 1023) == (id & 1023) && S.map[i].tag >= 0 &&
        S.map[i].tag <= 666)
      return S.map[i].tag;
  return -1;
}

// Lookup order. Private tags are never in the skin packet - they only arrive
// through the :9000 relay map (keyed by salted ntlid) - so the map and the team
// roster are checked BEFORE the packet tag (the packet can hold placeholder
// ids such as 666/667 for private wearers).
// own worn tag -> team roster -> map (salted ntlid) -> skin-packet tag ->
// map (raw id) -> map (low 10 bits).
static int resolve_snake_tag(snake *o, bool is_local, const user_settings *us) {
  if (is_local && us->ntl_tag_id >= 0 && ntl_tags_exists(us->ntl_tag_id))
    return us->ntl_tag_id;
  int team_tag = ntl_team_tag_for_snake(o->ntl_id, us->server_address);
  if (team_tag >= 0 && ntl_tags_exists(team_tag)) return team_tag;
  int t = map_lookup_exact((int)o->ntl_id);
  if (t >= 0) return t;
  if (o->ntl_packet_tag_id >= 0 && ntl_tags_exists(o->ntl_packet_tag_id))
    return o->ntl_packet_tag_id;
  t = map_lookup_exact(o->id & 0xFFFF);
  if (t >= 0) return t;
  return map_lookup_low10(o->id & 0xFFFF);
}

static void tag_ws_cb(struct mg_connection *c, int ev, void *ev_data) {
  bool node_a = c == S.ws;
  bool node_b = c == S.ws2;
  if (!node_a && !node_b) return;
  if (ev == MG_EV_WS_OPEN) {
    if (node_a) {
      S.ws_open = true;
      S.handshake_sent = false;
      S.last_position_send = 0;
    } else {
      S.ws2_open = true;
      S.handshake2_sent = false;
      S.last_position_send2 = 0;
    }
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    if (wm->data.len >= 4 && wm->data.len % 4 == 0)
      apply_mapping((const uint8_t *)wm->data.buf, wm->data.len);
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    double retry = mg_millis() / 1000.0 + NTL_TAG_RECONNECT_SECONDS;
    if (node_b) retry += 8.0;  // secondary node: don't hammer it if it's down
    if (node_a) {
      S.ws = NULL;
      S.ws_open = false;
      S.handshake_sent = false;
      S.last_position_send = 0;
      S.next_connect = retry;
    } else {
      S.ws2 = NULL;
      S.ws2_open = false;
      S.handshake2_sent = false;
      S.last_position_send2 = 0;
      S.next_connect2 = retry;
    }
  }
}

// node 0 = primary, node 1 = secondary. Same handshake as vlither-main:
// ["<sid><nick>","<server>","<tag md5>",tag,skin,x,y,ntlid]
static void send_tag_handshake(snake *me, int node) {
  struct mg_connection *conn = node ? S.ws2 : S.ws;
  bool open = node ? S.ws2_open : S.ws_open;
  if (!conn || !open || !S.env || !S.env->usr || !me) return;
  user_settings *us = &S.env->usr->usrs;

  if (node && S.sid2[0] == 0) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) S.sid2[i] = hex[rand() & 15];
    S.sid2[8] = 0;
  }
  const char *sid = node ? S.sid2 : us->ntl_client_id;

  char raw_nick[sizeof us->ntl_client_id + MAX_NICKNAME_LEN + 2];
  snprintf(raw_nick, sizeof raw_nick, "%s%s", sid,
           us->nickname[0] ? us->nickname : "Vlither");
  char nick[sizeof raw_nick * 2];
  char server[(MAX_SERVER_IP_LEN + 1) * 2];
  json_escape(nick, sizeof nick, raw_nick);
  json_escape(server, sizeof server, us->server_address);

  // the worn tag is broadcast whether public or private (private carries the
  // password md5), exactly like main. -1 / "" when nothing is worn.
  int tag = us->ntl_tag_id >= 0 ? us->ntl_tag_id : -1;
  const char *hash = (tag >= 0 && us->ntl_tag_password_md5[0])
                         ? us->ntl_tag_password_md5
                         : "";
  int skin = us->custom_skin ? 0 : us->default_skin;

  char json[512];
  int n = snprintf(json, sizeof json,
                   "[\"%s\",\"%s\",\"%s\",%d,%d,%d,%d,%u]",
                   nick, server, hash, tag, skin,
                   (int)lroundf(me->xx + me->fx),
                   (int)lroundf(me->yy + me->fy),
                   (unsigned)me->ntl_id);
  if (n > 0 && n < (int)sizeof json) {
    mg_ws_send(conn, json, (size_t)n, WEBSOCKET_OP_TEXT);
    double now = mg_millis() / 1000.0;
    if (node) {
      S.handshake2_sent = true;
      S.last_position_send2 = now;
    } else {
      S.handshake_sent = true;
      S.last_position_send = now;
    }
  }
}

static void send_position(snake *me, int node) {
  struct mg_connection *conn = node ? S.ws2 : S.ws;
  bool open = node ? S.ws2_open : S.ws_open;
  if (!conn || !open || !me) return;
  char json[96];
  int n = snprintf(json, sizeof json, "[%d,%d]",
                   (int)lroundf(me->xx + me->fx),
                   (int)lroundf(me->yy + me->fy));
  if (n > 0 && n < (int)sizeof json)
    mg_ws_send(conn, json, (size_t)n, WEBSOCKET_OP_TEXT);
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

static void finalize_pending_catalog_swaps(tenv *env) {
  if (!env || !env->ctx) return;
  if (tcontext_async_transfer_busy(env->ctx)) return;

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
  // decode + upload the atlas here (once) so opening the tag page never stalls
  if (S.env) ensure_atlas_loaded(S.env);

  double now = mg_millis() / 1000.0;
  if (S.auth && S.auth_started > 0.0 && now - S.auth_started > 12.0) {
    S.auth->is_closing = 1;
    S.auth = NULL;
    S.pending_tag_id = -1;
    set_status("NTL private-tag authorization timed out.");
    ntl_team_system_message(S.status);
  }
  if (S.authz && S.authz_started > 0.0 && now - S.authz_started > 12.0) {
    S.authz->is_closing = 1;
    S.authz = NULL;
    S.authz_started = 0.0;
    S.authz_next = now + 10.0;
  }
  if (!S.authz && now >= S.authz_next) start_authorize();
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
  bool allow_catalog = g->curr_screen != PLAYING || g->preview_active ||
                       g->conn != CONNECTED;
  if (!allow_catalog) {
    if (S.catalog) {
      S.catalog->is_closing = 1;
      S.catalog = NULL;
      S.catalog_started = 0.0;
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

  if (in_game) {
    int snake_count = tdarray_length(g->data.snakes);
    for (int i = 0; i < snake_count; ++i) {
      snake *o = &g->data.snakes[i];
      o->ntl_tag_id = resolve_snake_tag(o, o->id == g->data.snake_id, us);
    }
  }

  if (!in_game) {
    if (S.ws) S.ws->is_closing = 1;
    if (S.ws2) S.ws2->is_closing = 1;
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
    if (S.ws2) S.ws2->is_closing = 1;
    S.ws2 = NULL;
    S.ws2_open = false;
    reset_ws_handshake();
    S.next_connect = 0;
    S.next_connect2 = 0;
    // keep S.map so tags don't blink during the brief reconnect
  }

  const char *ws_hdr =
      "Origin: https://slither.io\r\nUser-Agent: Mozilla/5.0 (Linux; Android) Chrome/131.0 Mobile\r\n";
  if (!S.ws && now >= S.next_connect) {
    S.ws = mg_ws_connect(&S.mgr, NTL_TAG_WS_URL_A, tag_ws_cb, NULL, "%s",
                         ws_hdr);
    S.ws_connected_at = now;
    if (!S.ws) S.next_connect = now + NTL_TAG_RECONNECT_SECONDS;
  }
  if (!S.ws2 && now >= S.next_connect2) {
    S.ws2 = mg_ws_connect(&S.mgr, NTL_TAG_WS_URL_B, tag_ws_cb, NULL, "%s",
                          ws_hdr);
    S.ws2_connected_at = now;
    if (!S.ws2) S.next_connect2 = now + NTL_TAG_RECONNECT_SECONDS;
  }

  // hold the FIRST registration until our snake's salt is known (opcode 83), so
  // we never register the raw id first and flash the tag on other NTL screens.
  // fall back after 2.5s for servers that don't salt.
  bool salt_known = me->ntl_id != (uint16_t)(me->id & 0xFFFF);
  bool waiting_a = !salt_known && now - S.ws_connected_at < NTL_TAG_SALT_WAIT_SECONDS;
  bool waiting_b = !salt_known && now - S.ws2_connected_at < NTL_TAG_SALT_WAIT_SECONDS;

  if (S.ws_open && !S.handshake_sent && !waiting_a) send_tag_handshake(me, 0);
  if (S.ws_open && S.handshake_sent &&
      now - S.last_position_send >= NTL_TAG_POSITION_SECONDS) {
    send_position(me, 0);
    S.last_position_send = now;
  }
  if (S.ws2_open && !S.handshake2_sent && !waiting_b) send_tag_handshake(me, 1);
  if (S.ws2_open && S.handshake2_sent &&
      now - S.last_position_send2 >= NTL_TAG_POSITION_SECONDS) {
    send_position(me, 1);
    S.last_position_send2 = now;
  }

  if (ntl_tags_is_protected_tag(us->ntl_tag_id) &&
      us->ntl_tag_password_md5[0])
    me->ntl_tag_id = us->ntl_tag_id;
}

void ntl_tags_destroy(tenv *env) {
  if (!S.ready) return;
  tcontext *ctx = env ? env->ctx : (S.env ? S.env->ctx : NULL);
  if (ctx && (S.dynamic_atlas_tex_pending || S.fixed_atlas_tex_pending)) {
    tcontext_wait_idle(ctx);
    tcontext_poll_async_transfer(ctx);
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

// Shared by the tag page and the !tag chat command, so a code typed in NTL
// chat shows up in the tag page and vice versa.
static int private_id = 60;
static char private_password[96] = {0};

bool ntl_tags_request_private(int id, const char *password_in) {
  // same parsing as the chat: only the first whitespace-free word counts
  // (phone keyboards often add a trailing space / newline)
  char password[96] = {0};
  if (password_in)
    trim_copy(password, sizeof password, password_in, strlen(password_in));
  for (char *c = password; *c; ++c)
    if (isspace((unsigned char)*c)) {
      *c = 0;
      break;
    }
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

  private_id = id;
  strncpy(private_password, password, sizeof private_password - 1);
  private_password[sizeof private_password - 1] = 0;
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
               (ImVec2){0, 0})) {
    char cmd[160];
    snprintf(cmd, sizeof cmd, "!tag %d %s", private_id, private_password);
    ntl_tags_handle_command(cmd);
    memset(cmd, 0, sizeof cmd);
  }
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
    // build the list of public tags only when the catalogues change
    static const ntl_tag_atlas_entry
        *grid[NTL_TAG_ATLAS_ENTRY_COUNT + NTL_FIXED_TAG_MAX +
              NTL_DYNAMIC_TAG_MAX];
    static int grid_n = 0;
    static unsigned grid_key = 0;
    unsigned key = 2166136261u;
    key = (key ^ (unsigned)S.fixed_tag_count) * 16777619u;
    key = (key ^ (unsigned)S.dynamic_tag_count) * 16777619u;
    key = (key ^ (unsigned)S.public_tag_max) * 16777619u;
    for (int i = 0; i < S.fixed_tag_count; ++i)
      key = (key ^ (unsigned)S.fixed_atlas[i].id) * 16777619u;
    for (int i = 0; i < S.dynamic_tag_count; ++i)
      key = (key ^ (unsigned)S.dynamic_atlas[i].id) * 16777619u;
    if (key != grid_key || grid_n == 0) {
      grid_key = key;
      grid_n = 0;
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
        // live-catalogue entries that main's atlas already covers would show twice
        if (i >= NTL_TAG_ATLAS_ENTRY_COUNT && main_tag_lookup(a->id)) continue;
        // grid lists public tags only; private tags are applied by ID above
        if (!ntl_tags_is_packet_tag(a->id)) continue;
        grid[grid_n++] = a;
      }
    }

    // draw only the rows that are on screen
    const float row_h = tile + style->FramePadding.y * 2.0f +
                        style->ItemSpacing.y;
    const int rows = (grid_n + columns - 1) / columns;
    ImGuiListClipper *clip = ImGuiListClipper_ImGuiListClipper();
    ImGuiListClipper_Begin(clip, rows, row_h);
    while (ImGuiListClipper_Step(clip)) {
      for (int r = clip->DisplayStart; r < clip->DisplayEnd; ++r) {
        for (int c = 0; c < columns; ++c) {
          int idx = r * columns + c;
          if (idx >= grid_n) break;
          const ntl_tag_atlas_entry *a = grid[idx];
          if (c > 0) igSameLine(0, -1);
          VkDescriptorSet entry_ds = atlas_descriptor(env, a);
          if (!entry_ds) {
            igDummy((ImVec2){cell_w, row_h - style->ItemSpacing.y});
            continue;
          }
          ImTextureRef tex = {NULL, (ImTextureID)entry_ds};
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
            ntl_tags_select_public(a->id);
          }
          igPopStyleColor(2);
        }
      }
    }
    ImGuiListClipper_End(clip);
    ImGuiListClipper_destroy(clip);
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
  {
    const ntl_tag_def *td = main_tag_lookup(id);
    if (td && ensure_atlas_loaded(env)) {
      float custom_p = env->usr->usrs.tag_size_scale;
      if (!isfinite(custom_p) || custom_p < 0.50f || custom_p > 2.00f)
        custom_p = 1.0f;
      float rad_px = preview_snake_scale * 24.0f;
      tag_cord pc;
      tag_cord_build_straight(&pc, head_x, head_y, rad_px, angle);
      float unit = rad_px * 5.0f / 240.0f * custom_p;
      ntl_tag_render(igGetWindowDrawList(), (ImTextureID)(uintptr_t)S.atlas_ds,
                     td, pc.pls, pc.np, pc.wide, pc.ang, td->w * unit,
                     td->h * unit, 1.0f);
      return;
    }
  }
  const ntl_tag_atlas_entry *atlas = find_atlas_entry(id);
  if (!atlas) return;
  VkDescriptorSet atlas_ds = atlas_descriptor(env, atlas);
  if (!atlas_ds) return;
  const ntl_tag_meta *meta = find_meta(id);
  float custom = env->usr->usrs.tag_size_scale;
  if (!isfinite(custom) || custom < 0.50f || custom > 2.00f) custom = 1.0f;
  float body_scale = GLM_MAX(0.65f, preview_snake_scale) * custom;
  ImDrawList *dl = igGetWindowDrawList();
  float mc[3] = {0.08f, 0.45f, 0.65f}, ec[3] = {0.45f, 0.90f, 1.0f};
  if (meta) {
    for (int k = 0; k < 3; k++) {
      mc[k] = meta->c1[k] / 255.0f;
      ec[k] = meta->c2[k] / 255.0f;
    }
  }
  tag_cord cord;
  tag_cord_build_straight(&cord, head_x, head_y, preview_snake_scale * 24.0f,
                          angle);
  tag_cord_draw(dl, &cord, mc, ec, 1.0f);
  ImVec2 tip = cord.pls[cord.np - 1];
  // tag hangs from the cord end (same 12px-in anchor as in-game)
  ImVec2 center = {tip.x - cosf(angle) * 12.0f * body_scale,
                   tip.y - sinf(angle) * 12.0f * body_scale};
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

  // antenna: shared cord (follows the head trail), same as vlither-main
  float rad = roundf(o->sc * 29.0f) * 0.5f;
  tag_cord_view view = {mww2, mhh2, g->data.view_xx, g->data.view_yy,
                        g->data.gsc};
  tag_cord cord;
  tag_cord_build(&cord, o->id, 0, o->xx + o->fx, o->yy + o->fy, rad, o->ang,
                 &view, 0.0f);

  // main's atlas image + main's renderer (cord + banner anchored on its art)
  {
    const ntl_tag_def *td = main_tag_lookup(o->ntl_tag_id);
    if (td && ensure_atlas_loaded(env)) {
      const user_settings *us = &env->usr->usrs;
      float custom_s = us->tag_size_scale;
      if (!isfinite(custom_s) || custom_s < 0.50f || custom_s > 2.00f)
        custom_s = 1.0f;
      // banner px = image px * (rad*5/240) * zoom, same as main
      float unit = us->tag_size_with_zoom
                       ? rad * 5.0f / 240.0f * g->data.gsc
                       : 14.5f * 5.0f / 240.0f;
      unit *= custom_s;
      ntl_tag_render(igGetWindowDrawList(),
                     (ImTextureID)(uintptr_t)S.atlas_ds, td, cord.pls, cord.np,
                     cord.wide, cord.ang, td->w * unit, td->h * unit, alpha);
      return;
    }
  }

  ImDrawList *dl = igGetWindowDrawList();
  float mc[3] = {0.1f, 0.55f, 0.7f}, ec[3] = {0.45f, 0.9f, 1.0f};
  if (meta) {
    for (int k = 0; k < 3; k++) {
      mc[k] = meta->c1[k] / 255.0f;
      ec[k] = meta->c2[k] / 255.0f;
    }
  }
  tag_cord_draw(dl, &cord, mc, ec, alpha);

  // tag hangs from the cord end, oriented along the cord
  ImVec2 tip = cord.pls[cord.np - 1];
  float angle = cord.ang - PI * 0.5f;
  float ux = cosf(angle), uy = sinf(angle);
  float vx = -uy, vy = ux;
  ImVec2 center = {tip.x + vx * 12.0f * body_scale,
                   tip.y + vy * 12.0f * body_scale};

  const ntl_tag_atlas_entry *atlas = find_atlas_entry(o->ntl_tag_id);
  VkDescriptorSet atlas_ds = atlas_descriptor(env, atlas);
  if (!atlas || !atlas_ds) {
    draw_fallback(dl, center, o->ntl_tag_id, alpha, body_scale);
    return;
  }

  float width, height;
  tag_image_dimensions(meta, atlas, body_scale, &width, &height);
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
