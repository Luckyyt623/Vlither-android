#include "live_feed.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../external/mongoose.h"
#include "../user.h"

typedef struct {
  const char* src;
  const char* host;
} live_region;

static const live_region REGIONS[] = {
#ifdef LIVE_FEED_TEST_HOST
    {"EU", LIVE_FEED_TEST_HOST},
#endif
    {"EU", "81.169.165.225"}, {"US", "45.77.5.207"},
    {"IN", "139.84.170.60"},  {"JP", "45.76.102.107"},
    {"BR", "216.128.169.33"}, {"AE", "130.94.45.154"},
    {"KR", "141.164.36.225"}, {"SG", "5.223.78.163"},
};

static const char* region_for(const char* addr, char* host_out, size_t cap) {
  if (!addr || !addr[0]) return NULL;
  char host[64];
  if (!strncmp(addr, "wss://", 6)) addr += 6;
  else if (!strncmp(addr, "ws://", 5)) addr += 5;
  const char* colon = strchr(addr, ':');
  size_t hl = colon ? (size_t)(colon - addr) : strlen(addr);
  if (hl == 0 || hl >= sizeof host) return NULL;
  memcpy(host, addr, hl);
  host[hl] = 0;
  int port = colon ? atoi(colon + 1) : 444;
  for (size_t i = 0; i < sizeof REGIONS / sizeof REGIONS[0]; ++i) {
    if (strcmp(host, REGIONS[i].host)) continue;
    if (port == 444) {
      if (host_out) snprintf(host_out, cap, "%s", host);
      return REGIONS[i].src;
    }
    return NULL;
  }
  return NULL;
}

static const uint8_t PAL_R[41] = {192,144,128,128,238,255,255,255,224,255,144,80,255,40,100,120,72,160,255,56,56,78,255,101,128,60,0,217,255,144,32,240,240,240,240,32,40,104,0,104,0};
static const uint8_t PAL_G[41] = {128,153,208,255,238,160,144,64,48,255,153,80,192,136,117,134,84,80,224,68,68,35,86,200,132,192,255,69,64,144,32,32,240,144,32,240,60,128,0,40,0};
static const uint8_t PAL_B[41] = {255,255,208,128,112,96,144,64,224,255,255,80,80,96,255,255,255,255,64,255,255,192,9,232,144,72,83,69,64,144,240,32,32,32,240,32,173,255,112,170,0};

static ImU32 pal_col(int cv, int alpha) {
  int i = ((cv % 41) + 41) % 41;
  return ((ImU32)alpha << 24) | ((ImU32)PAL_B[i] << 16) |
         ((ImU32)PAL_G[i] << 8) | (ImU32)PAL_R[i];
}

#define DOT_BUCKETS (1 << 15)
#define DOT_MAX (1 << 20)

typedef struct {
  uint16_t x, y;
  uint8_t kind, cv;
  int32_t next;
} live_dot;

typedef struct {
  live_dot* d;
  int n, cap;
  int32_t free_head;
  int32_t* heads;
  int count, text_count;
  uint32_t ver;
} dot_table;

static uint32_t cell_bucket(int cx, int cy) {
  return (((uint32_t)cx * 73856093u) ^ ((uint32_t)cy * 19349663u)) &
         (DOT_BUCKETS - 1);
}

static void dots_clear(dot_table* t) {
  if (!t->heads) t->heads = (int32_t*)malloc(sizeof(int32_t) * DOT_BUCKETS);
  if (!t->heads) return;
  for (int i = 0; i < DOT_BUCKETS; ++i) t->heads[i] = -1;
  t->n = 0;
  t->free_head = -1;
  t->count = 0;
  t->text_count = 0;
  t->ver++;
}

static void dots_free(dot_table* t) {
  free(t->d);
  free(t->heads);
  memset(t, 0, sizeof *t);
  t->free_head = -1;
}

static int32_t dots_find(dot_table* t, uint16_t x, uint16_t y) {
  if (!t->heads) return -1;
  uint32_t b = cell_bucket(x >> 2, y >> 2);
  for (int32_t i = t->heads[b]; i >= 0; i = t->d[i].next)
    if (t->d[i].x == x && t->d[i].y == y) return i;
  return -1;
}

static void dots_remove(dot_table* t, uint16_t x, uint16_t y) {
  if (!t->heads) return;
  uint32_t b = cell_bucket(x >> 2, y >> 2);
  int32_t prev = -1;
  for (int32_t i = t->heads[b]; i >= 0; prev = i, i = t->d[i].next) {
    if (t->d[i].x != x || t->d[i].y != y) continue;
    if (prev < 0)
      t->heads[b] = t->d[i].next;
    else
      t->d[prev].next = t->d[i].next;
    t->d[i].next = t->free_head;
    if (t->d[i].kind == LIVE_DOT_TEXT && t->text_count > 0) t->text_count--;
    t->d[i].kind = 0;
    t->free_head = i;
    t->count--;
    t->ver++;
    return;
  }
}

static void dots_set(dot_table* t, uint16_t x, uint16_t y, uint8_t kind, uint8_t cv) {
  if (!t->heads) return;
  int32_t f = dots_find(t, x, y);
  if (f >= 0) {
    if (t->d[f].kind != kind || t->d[f].cv != cv) t->ver++;
    if (t->d[f].kind != kind) {
      if (t->d[f].kind == LIVE_DOT_TEXT && t->text_count > 0) t->text_count--;
      if (kind == LIVE_DOT_TEXT) t->text_count++;
    }
    t->d[f].kind = kind;
    t->d[f].cv = cv;
    return;
  }
  int32_t idx;
  if (t->free_head >= 0) {
    idx = t->free_head;
    t->free_head = t->d[idx].next;
  } else {
    if (t->n >= DOT_MAX) return;
    if (t->n >= t->cap) {
      int nc = t->cap ? t->cap * 2 : 4096;
      live_dot* nd = (live_dot*)realloc(t->d, sizeof(live_dot) * nc);
      if (!nd) return;
      t->d = nd;
      t->cap = nc;
    }
    idx = t->n++;
  }
  uint32_t b = cell_bucket(x >> 2, y >> 2);
  t->d[idx].x = x;
  t->d[idx].y = y;
  t->d[idx].kind = kind;
  if (kind == LIVE_DOT_TEXT) t->text_count++;
  t->d[idx].cv = cv;
  t->d[idx].next = t->heads[b];
  t->heads[b] = idx;
  t->count++;
  t->ver++;
}

#define MAX_SHAPES 512
#define MAX_TEXTS 256
#define MAX_RECIP 16
#define MAX_FLOATS 400000

typedef struct {
  int kind, cv;
  float dot;
  bool has_circle, closed;
  float circle[3];
  float* pts;
  int npts;
  float* dots;
  int ndots;
} live_shape;

typedef struct {
  char text[96];
  int cv;
  float cell, x, y;
  bool has_arc, viewer_specific, is_viewer_text;
  float arc_mid, arc_r, arc_cx, arc_cy;
  int nrecip;
  int recip[MAX_RECIP];
} live_text;

typedef struct {
  live_shape* shapes;
  int ns;
  live_text* texts;
  int nt;
} live_frame;

static void frame_free(live_frame* f) {
  for (int i = 0; i < f->ns; ++i) {
    free(f->shapes[i].pts);
    free(f->shapes[i].dots);
  }
  free(f->shapes);
  free(f->texts);
  memset(f, 0, sizeof *f);
}

typedef struct {
  const char* p;
  const char* e;
  bool err;
} jr;

static void jws(jr* j) {
  while (j->p < j->e &&
         (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t'))
    j->p++;
}
static bool jeat(jr* j, char c) {
  jws(j);
  if (j->p < j->e && *j->p == c) {
    j->p++;
    return true;
  }
  return false;
}
static bool jpeek(jr* j, char c) {
  jws(j);
  return j->p < j->e && *j->p == c;
}
static double jnum(jr* j) {
  jws(j);
  if (j->p < j->e && (*j->p == 'n' || *j->p == 't' || *j->p == 'f')) {

    bool yes = *j->p == 't';
    while (j->p < j->e && isalpha((unsigned char)*j->p)) j->p++;
    return yes ? 1.0 : 0.0;
  }
  char buf[48];
  size_t n = 0;
  while (j->p < j->e && n < sizeof buf - 1 &&
         (isdigit((unsigned char)*j->p) || *j->p == '-' || *j->p == '+' ||
          *j->p == '.' || *j->p == 'e' || *j->p == 'E'))
    buf[n++] = *j->p++;
  buf[n] = 0;
  if (!n) j->err = true;
  return atof(buf);
}
static int jhex4(jr* j) {
  int v = 0;
  for (int i = 0; i < 4; ++i) {
    if (j->p >= j->e || !isxdigit((unsigned char)*j->p)) {
      j->err = true;
      return 0;
    }
    char c = *j->p++;
    v = v * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
  }
  return v;
}
static size_t put_utf8(char* o, size_t cap, size_t n, unsigned cp) {
  char t[4];
  int l;
  if (cp < 0x80) { t[0] = (char)cp; l = 1; }
  else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 63)); l = 2; }
  else if (cp < 0x10000) { t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 63)); t[2] = (char)(0x80 | (cp & 63)); l = 3; }
  else { t[0] = (char)(0xF0 | (cp >> 18)); t[1] = (char)(0x80 | ((cp >> 12) & 63)); t[2] = (char)(0x80 | ((cp >> 6) & 63)); t[3] = (char)(0x80 | (cp & 63)); l = 4; }
  if (o && n + l < cap) memcpy(o + n, t, l), n += l;
  return n;
}

static void jstr(jr* j, char* out, size_t cap) {
  jws(j);
  if (j->p >= j->e || *j->p != '"') { j->err = true; return; }
  j->p++;
  size_t n = 0;
  while (j->p < j->e && *j->p != '"') {
    if (*j->p == '\\') {
      j->p++;
      if (j->p >= j->e) { j->err = true; return; }
      char c = *j->p++;
      unsigned cp = 0;
      switch (c) {
        case 'n': cp = '\n'; break;
        case 't': cp = '\t'; break;
        case 'r': cp = '\r'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'u': {
          cp = (unsigned)jhex4(j);
          if (cp >= 0xD800 && cp < 0xDC00 && j->p + 1 < j->e &&
              j->p[0] == '\\' && j->p[1] == 'u') {
            j->p += 2;
            unsigned lo = (unsigned)jhex4(j);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          break;
        }
        default: cp = (unsigned char)c; break;
      }
      n = put_utf8(out, cap, n, cp);
    } else {
      if (out && n + 1 < cap) out[n] = *j->p;
      n++;
      j->p++;
    }
  }
  if (j->p >= j->e) { j->err = true; return; }
  j->p++;
  if (out && cap) out[n < cap ? n : cap - 1] = 0;
}
static void jskip(jr* j);
static void jskip_container(jr* j, char open, char close) {
  (void)open;
  j->p++;
  if (jeat(j, close)) return;
  for (;;) {
    if (open == '{') { jstr(j, NULL, 0); if (!jeat(j, ':')) { j->err = true; return; } }
    jskip(j);
    if (j->err) return;
    if (jeat(j, ',')) continue;
    if (jeat(j, close)) return;
    j->err = true;
    return;
  }
}
static void jskip(jr* j) {
  jws(j);
  if (j->p >= j->e) { j->err = true; return; }
  char c = *j->p;
  if (c == '{') jskip_container(j, '{', '}');
  else if (c == '[') jskip_container(j, '[', ']');
  else if (c == '"') jstr(j, NULL, 0);
  else {
    while (j->p < j->e && *j->p != ',' && *j->p != '}' && *j->p != ']' &&
           !isspace((unsigned char)*j->p))
      j->p++;
  }
}
static bool jbool(jr* j) {
  jws(j);
  bool v = j->p < j->e && *j->p == 't';
  jskip(j);
  return v;
}

static float* jfloats(jr* j, int* count) {
  *count = 0;
  jws(j);
  if (j->p >= j->e || *j->p != '[') { jskip(j); return NULL; }
  j->p++;
  int cap = 0, n = 0;
  float* a = NULL;
  if (jeat(j, ']')) return NULL;
  for (;;) {
    jws(j);
    if (j->p < j->e && !(isdigit((unsigned char)*j->p) || *j->p == '-' ||
                         *j->p == '+' || *j->p == '.')) {
      j->err = true;
      free(a);
      return NULL;
    }
    double v = jnum(j);
    if (j->err) { free(a); return NULL; }
    if (n < MAX_FLOATS) {
      if (n >= cap) {
        int nc = cap ? cap * 2 : 64;
        float* na = (float*)realloc(a, sizeof(float) * nc);
        if (!na) { free(a); j->err = true; return NULL; }
        a = na;
        cap = nc;
      }
      a[n++] = (float)v;
    }
    if (jeat(j, ',')) continue;
    if (jeat(j, ']')) break;
    j->err = true;
    free(a);
    return NULL;
  }
  *count = n;
  return a;
}

static void parse_shape(jr* j, live_shape* s) {
  memset(s, 0, sizeof *s);
  s->dot = 1;
  if (!jeat(j, '{')) { j->err = true; return; }
  if (jeat(j, '}')) return;
  for (;;) {
    char key[24];
    jstr(j, key, sizeof key);
    if (!jeat(j, ':')) { j->err = true; return; }
    if (!strcmp(key, "cv")) s->cv = (int)jnum(j);
    else if (!strcmp(key, "kind")) s->kind = (int)jnum(j);
    else if (!strcmp(key, "dot")) s->dot = (float)jnum(j);
    else if (!strcmp(key, "closed")) s->closed = jbool(j);
    else if (!strcmp(key, "circle")) {
      int n = 0;
      float* a = jfloats(j, &n);
      if (a && n >= 3) {
        s->has_circle = true;
        memcpy(s->circle, a, sizeof(float) * 3);
      }
      free(a);
    } else if (!strcmp(key, "pts")) {
      free(s->pts);
      s->pts = jfloats(j, &s->npts);
    } else if (!strcmp(key, "dots")) {
      free(s->dots);
      s->dots = jfloats(j, &s->ndots);
    } else jskip(j);
    if (j->err) return;
    if (jeat(j, ',')) continue;
    if (jeat(j, '}')) return;
    j->err = true;
    return;
  }
}

static void parse_arc(jr* j, live_text* t) {
  if (!jpeek(j, '{')) { jskip(j); return; }
  j->p++;
  t->has_arc = true;
  if (jeat(j, '}')) return;
  for (;;) {
    char key[16];
    jstr(j, key, sizeof key);
    if (!jeat(j, ':')) { j->err = true; return; }
    if (!strcmp(key, "mid")) t->arc_mid = (float)jnum(j);
    else if (!strcmp(key, "r")) t->arc_r = (float)jnum(j);
    else if (!strcmp(key, "cx")) t->arc_cx = (float)jnum(j);
    else if (!strcmp(key, "cy")) t->arc_cy = (float)jnum(j);
    else jskip(j);
    if (j->err) return;
    if (jeat(j, ',')) continue;
    if (jeat(j, '}')) break;
    j->err = true;
    return;
  }
  if (t->arc_r <= 0) t->has_arc = false;
}

static void parse_recipients(jr* j, live_text* t) {
  if (!jpeek(j, '[')) { jskip(j); return; }
  j->p++;
  if (jeat(j, ']')) return;
  for (;;) {
    if (!jpeek(j, '{')) { jskip(j); }
    else {
      j->p++;
      int sid = -1;
      if (!jeat(j, '}')) {
        for (;;) {
          char key[16];
          jstr(j, key, sizeof key);
          if (!jeat(j, ':')) { j->err = true; return; }
          if (!strcmp(key, "snake_id")) sid = (int)jnum(j);
          else jskip(j);
          if (j->err) return;
          if (jeat(j, ',')) continue;
          if (jeat(j, '}')) break;
          j->err = true;
          return;
        }
      }
      if (sid >= 0 && t->nrecip < MAX_RECIP) t->recip[t->nrecip++] = sid;
    }
    if (j->err) return;
    if (jeat(j, ',')) continue;
    if (jeat(j, ']')) return;
    j->err = true;
    return;
  }
}

static void parse_text(jr* j, live_text* t, bool viewer_text) {
  memset(t, 0, sizeof *t);
  t->cell = 1;
  t->is_viewer_text = viewer_text;
  if (!jeat(j, '{')) { j->err = true; return; }
  if (jeat(j, '}')) return;
  for (;;) {
    char key[20];
    jstr(j, key, sizeof key);
    if (!jeat(j, ':')) { j->err = true; return; }
    if (!strcmp(key, "text")) {
      if (jpeek(j, '"')) jstr(j, t->text, sizeof t->text);
      else jskip(j);
    }
    else if (!strcmp(key, "cv")) t->cv = (int)jnum(j);
    else if (!strcmp(key, "cell")) t->cell = (float)jnum(j);
    else if (!strcmp(key, "x")) t->x = (float)jnum(j);
    else if (!strcmp(key, "y")) t->y = (float)jnum(j);
    else if (!strcmp(key, "arc")) parse_arc(j, t);
    else if (!strcmp(key, "viewer_specific")) t->viewer_specific = jbool(j);
    else if (!strcmp(key, "recipients")) parse_recipients(j, t);
    else jskip(j);
    if (j->err) return;
    if (jeat(j, ',')) continue;
    if (jeat(j, '}')) return;
    j->err = true;
    return;
  }
}

static char s_json_err[96];
static int s_json_bad_elems;

static void note_json_error(const char* base, const char* at, const char* end) {
  size_t off = (size_t)(at - base);
  size_t n = (size_t)(end - at);
  if (n > 28) n = 28;
  char ctx[32];
  size_t k = 0;
  for (size_t i = 0; i < n && k < sizeof ctx - 1; ++i)
    ctx[k++] = (unsigned char)at[i] < 32 ? ' ' : at[i];
  ctx[k] = 0;
  snprintf(s_json_err, sizeof s_json_err, "json error @%zu near '%s'", off, ctx);
}

static bool parse_frame(const char* text, size_t len, live_frame* out) {
  memset(out, 0, sizeof *out);
  s_json_bad_elems = 0;
  out->shapes = (live_shape*)calloc(MAX_SHAPES, sizeof(live_shape));
  out->texts = (live_text*)calloc(MAX_TEXTS, sizeof(live_text));
  if (!out->shapes || !out->texts) { frame_free(out); return false; }
  jr j = {text, text + len, false};
  if (!jeat(&j, '{')) { note_json_error(text, j.p, j.e); frame_free(out); return false; }
  if (!jeat(&j, '}')) {
    for (;;) {
      char key[24];
      jstr(&j, key, sizeof key);
      if (!jeat(&j, ':')) j.err = true;
      if (j.err) break;
      bool is_shapes = !strcmp(key, "shapes");
      bool is_texts = !strcmp(key, "texts");
      bool is_vtexts = !strcmp(key, "viewer_texts");
      if ((is_shapes || is_texts || is_vtexts) && jpeek(&j, '[')) {
        j.p++;
        if (!jeat(&j, ']')) {
          for (;;) {

            jws(&j);
            const char* st = j.p;
            jskip(&j);
            if (j.err) break;
            jr sub = {st, j.p, false};
            if (is_shapes) {
              live_shape tmp;
              parse_shape(&sub, &tmp);
              if (sub.err) { free(tmp.pts); free(tmp.dots); s_json_bad_elems++; }
              else if (out->ns < MAX_SHAPES) out->shapes[out->ns++] = tmp;
              else { free(tmp.pts); free(tmp.dots); }
            } else {
              live_text tmp;
              parse_text(&sub, &tmp, is_vtexts);
              if (sub.err) s_json_bad_elems++;
              else if (out->nt < MAX_TEXTS) out->texts[out->nt++] = tmp;
            }
            if (jeat(&j, ',')) continue;
            if (jeat(&j, ']')) break;
            j.err = true;
            break;
          }
        }
      } else jskip(&j);
      if (j.err) break;
      if (jeat(&j, ',')) continue;
      if (jeat(&j, '}')) break;
      j.err = true;
      break;
    }
  }
  if (j.err) { note_json_error(text, j.p, j.e); frame_free(out); return false; }
  s_json_err[0] = 0;
  return true;
}

typedef struct { float x0, y0, x1, y1, w; uint8_t cv; } fb_prim;
typedef struct { float x, y, nd; uint8_t cv; } fb_pt;

static struct {
  fb_prim* p;
  int n, cap;
  uint32_t ver;
  double t;
  bool built;
} FB[2];

static void fb_free(void);

static struct {
  bool ready;
  struct mg_mgr mgr;
  struct mg_connection* ws;
  bool ws_open;
  bool have_keyframe;
  char src[4];
  double next_connect;
  double last_resync;
  dot_table dots;
  live_frame frame;
  int shape_dots, text_dots;
  char status[256];
  char err[80];
  int json_len, json_ok, json_fail;
  char host[96];
} S;

static void set_status(const char* s) {
  snprintf(S.status, sizeof S.status, "%s", s);
}

static void reset_data(void) {
  dots_clear(&S.dots);
  frame_free(&S.frame);
  S.have_keyframe = false;
  S.json_ok = S.json_fail = 0;
  FB[0].built = FB[1].built = false;
}

static inline uint16_t b16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t b32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void pellet_apply(const uint8_t* p) {
  uint8_t ui = p[11];
  uint16_t x = b16(p + 7), y = b16(p + 9);
  if (ui) dots_set(&S.dots, x, y, (ui & 0x80) ? LIVE_DOT_TEXT : LIVE_DOT_SHAPE, p[4]);
  else dots_remove(&S.dots, x, y);
}

static void on_keyframe(const uint8_t* d, size_t len) {
  if (len < 17) return;
  size_t o = 15;
  uint16_t ns = b16(d + o);
  o += 2;
  for (uint16_t i = 0; i < ns; ++i) {
    if (o + 8 > len) return;
    o += 8 + d[o + 7];
    if (o + 2 > len) return;
    uint16_t nb = b16(d + o);
    o += 2 + (size_t)nb * 4;
    if (o + 1 > len) return;
    o += 1 + d[o];
    o += 4;
    if (o > len) return;
  }
  if (o + 4 > len) return;
  uint32_t nf = b32(d + o);
  o += 4;
  dots_clear(&S.dots);
  for (uint32_t i = 0; i < nf && o + 12 <= len; ++i, o += 12)
    if (d[o + 11]) pellet_apply(d + o);
  S.have_keyframe = true;
}

static void on_events(const uint8_t* d, size_t len) {
  if (len < 7) return;
  uint16_t n = b16(d + 5);
  size_t o = 7;
  for (uint16_t i = 0; i < n && o + 5 <= len; ++i) {
    uint8_t type = d[o];
    size_t plen = b16(d + o + 3);
    if (o + 5 + plen > len) break;
    const uint8_t* p = d + o + 5;
    if (type == 7 && plen >= 12) pellet_apply(p);
    else if (type == 8 && plen >= 8) dots_remove(&S.dots, b16(p + 4), b16(p + 6));
    o += 5 + plen;
  }
}

static void on_binary(const uint8_t* d, size_t len) {
  if (len < 1) return;
  if (d[0] == 0x4B) on_keyframe(d, len);
  else if (d[0] == 0x45 && S.have_keyframe) on_events(d, len);
}

static void on_json(const char* s, size_t len) {
  live_frame f;
  S.json_len = (int)len;
  if (!parse_frame(s, len, &f)) { S.json_fail++; return; }
  S.json_ok++;
  frame_free(&S.frame);
  S.frame = f;
}

static void ws_cb(struct mg_connection* c, int ev, void* ev_data) {
  if (c != S.ws) return;
  if (ev == MG_EV_WS_OPEN) {
    S.ws_open = true;
    S.last_resync = mg_millis() / 1000.0;
    mg_ws_send(c, "resync", 6, WEBSOCKET_OP_TEXT);
    snprintf(S.status, sizeof S.status, "Live feed: connected to %s, waiting for data", S.host);
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
    int op = wm->flags & 0x0f;
    if (op == WEBSOCKET_OP_BINARY)
      on_binary((const uint8_t*)wm->data.buf, wm->data.len);
    else if (op == WEBSOCKET_OP_TEXT)
      on_json(wm->data.buf, wm->data.len);
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (ev == MG_EV_ERROR && ev_data)
      snprintf(S.err, sizeof S.err, "%s", (const char*)ev_data);
    bool was_open = S.ws_open;
    S.ws = NULL;
    S.ws_open = false;
    S.next_connect = mg_millis() / 1000.0 + 3.0;
    reset_data();
    if (S.err[0])
      snprintf(S.status, sizeof S.status, "Live feed: %s (%s), retrying", S.err, S.host);
    else
      snprintf(S.status, sizeof S.status, "Live feed: %s (%s), retrying",
               was_open ? "connection lost" : "relay closed the connection", S.host);
  }
}

void live_feed_init(tenv* env) {
  (void)env;
  memset(&S, 0, sizeof S);
  S.dots.free_head = -1;
  mg_mgr_init(&S.mgr);
  dots_clear(&S.dots);
  set_status("Live feed: off");
  S.ready = true;
}

static void close_ws(void) {
  if (S.ws) S.ws->is_closing = 1;
  S.ws = NULL;
  S.ws_open = false;
}

void live_feed_update(tenv* env) {
  if (!S.ready || !env || !env->usr) return;
  mg_mgr_poll(&S.mgr, 0);

  user_settings* us = &env->usr->usrs;
  game_data* g = &env->usr->gdata;
  bool wanted = (user_clean_lines(us) || user_clean_text(us));
  bool playing = g->conn == CONNECTED && g->curr_screen == PLAYING &&
                 !g->preview_active;
  char game_host[64] = "";
  const char* src = (wanted && playing)
                        ? region_for(us->server_address, game_host, sizeof game_host)
                        : NULL;

  if (!wanted) {
    if (S.ws || S.have_keyframe) { close_ws(); reset_data(); }
    set_status("Live feed: off");
    return;
  }
  if (!playing) return;
  if (!src) {
    if (S.ws) { close_ws(); reset_data(); }
    snprintf(S.status, sizeof S.status, "Live feed: no feed for server \"%s\"",
             us->server_address);
    return;
  }
  if (S.ws && strcmp(S.src, src)) { close_ws(); reset_data(); }

  bool direct = strstr(VLITHER_LIVE_RELAY_URL, "YOUR-RELAY-HOST") != NULL;
  double now = mg_millis() / 1000.0;
  if (!S.ws && now >= S.next_connect) {
    char url[256];
    if (direct)
      snprintf(url, sizeof url, "ws://%s:444/live", game_host);
    else
      snprintf(url, sizeof url, "%s?src=%s", VLITHER_LIVE_RELAY_URL, src);
    snprintf(S.src, sizeof S.src, "%s", src);
    struct mg_str h = mg_url_host(url);
    snprintf(S.host, sizeof S.host, "%.*s:%u", (int)h.len, h.buf,
             (unsigned)mg_url_port(url));
    S.err[0] = 0;
    S.ws = mg_ws_connect(&S.mgr, url, ws_cb, NULL, NULL);
    if (!S.ws) {
      S.next_connect = now + 3.0;
      snprintf(S.status, sizeof S.status, "Live feed: could not start connection to %s", S.host);
    } else {
      snprintf(S.status, sizeof S.status, "Live feed: connecting to %s...", S.host);
    }
  }
  if (S.ws && S.ws_open && !S.have_keyframe && now - S.last_resync >= 3.0) {
    S.last_resync = now;
    mg_ws_send(S.ws, "resync", 6, WEBSOCKET_OP_TEXT);
  }
  if (S.ws && S.ws_open && S.have_keyframe) {
    int n = snprintf(S.status, sizeof S.status,
                     "Live feed (%s): %d dots, %d shapes, %d texts (json %d ok / %d bad)",
                     S.src, S.dots.count, S.frame.ns, S.frame.nt, S.json_ok, S.json_fail);
    if (S.json_fail && s_json_err[0] && n > 0 && (size_t)n < sizeof S.status)
      snprintf(S.status + n, sizeof S.status - n, " %s", s_json_err);
    else if (s_json_bad_elems && n > 0 && (size_t)n < sizeof S.status)
      snprintf(S.status + n, sizeof S.status - n, " (%d entries skipped)", s_json_bad_elems);
  }
}

void live_feed_destroy(tenv* env) {
  (void)env;
  if (!S.ready) return;
  close_ws();
  mg_mgr_free(&S.mgr);
  dots_free(&S.dots);
  frame_free(&S.frame);
  fb_free();
  memset(&S, 0, sizeof S);
}

bool live_feed_active(void) { return S.ready && S.ws_open && S.have_keyframe; }
bool live_feed_has_text(void) { return live_feed_active() && (S.frame.nt > 0 || S.dots.text_count > 0); }
const char* live_feed_status(void) { return S.status; }

int live_feed_dot_kind(float x, float y) {
  if (!live_feed_active() || !S.dots.count) return LIVE_DOT_NONE;
  const float tol = 2.0f;
  int cx0 = (int)floorf((x - tol) / 4.0f), cx1 = (int)floorf((x + tol) / 4.0f);
  int cy0 = (int)floorf((y - tol) / 4.0f), cy1 = (int)floorf((y + tol) / 4.0f);
  int kind = LIVE_DOT_NONE;
  float best = FLT_MAX;
  for (int cx = cx0; cx <= cx1; ++cx)
    for (int cy = cy0; cy <= cy1; ++cy) {
      uint32_t b = cell_bucket(cx, cy);
      for (int32_t i = S.dots.heads[b]; i >= 0; i = S.dots.d[i].next) {
        const live_dot* d = &S.dots.d[i];
        float dx = d->x - x, dy = d->y - y;
        if (fabsf(dx) > tol || fabsf(dy) > tol) continue;
        float dist = dx * dx + dy * dy;
        if (dist < best) { best = dist; kind = d->kind; }
      }
    }
  return kind;
}

#define CLEAN_ALPHA 235

static ImVec2* s_pts;
static int s_pts_cap;

static ImVec2* pts_buf(int n) {
  if (n > s_pts_cap) {
    int nc = n < 256 ? 256 : n;
    ImVec2* nb = (ImVec2*)realloc(s_pts, sizeof(ImVec2) * nc);
    if (!nb) return NULL;
    s_pts = nb;
    s_pts_cap = nc;
  }
  return s_pts;
}

static void draw_shapes(ImDrawList* draw, float vx, float vy, float zoom,
                        float hw, float hh) {
  for (int i = 0; i < S.frame.ns; ++i) {
    const live_shape* s = &S.frame.shapes[i];
    ImU32 col = pal_col(s->cv, CLEAN_ALPHA);
    float lw = fmaxf(1.0f, 2.0f * s->dot * zoom);
    if (s->has_circle) {
      ImVec2 c = {hw + (s->circle[0] - vx) * zoom, hh + (s->circle[1] - vy) * zoom};
      float r = s->circle[2] * zoom;
      if (r < 0.5f) continue;

      float dc = sqrtf((c.x - hw) * (c.x - hw) + (c.y - hh) * (c.y - hh));
      if (fabsf(dc - r) > sqrtf(hw * hw + hh * hh) + lw) continue;
      ImDrawList_AddCircle(draw, c, r, col, 0, lw);
    } else if (s->npts >= 4 && s->pts) {
      int n = s->npts / 2;
      ImVec2* b = pts_buf(n);
      if (!b) continue;
      float mnx = FLT_MAX, mny = FLT_MAX, mxx = -FLT_MAX, mxy = -FLT_MAX;
      for (int k = 0; k < n; ++k) {
        b[k].x = hw + (s->pts[2 * k] - vx) * zoom;
        b[k].y = hh + (s->pts[2 * k + 1] - vy) * zoom;
        mnx = fminf(mnx, b[k].x); mxx = fmaxf(mxx, b[k].x);
        mny = fminf(mny, b[k].y); mxy = fmaxf(mxy, b[k].y);
      }
      if (mxx < -lw || mnx > 2 * hw + lw || mxy < -lw || mny > 2 * hh + lw) continue;
      ImDrawList_AddPolyline(draw, b, n, col,
                             s->closed ? ImDrawFlags_Closed : ImDrawFlags_None, lw);
    } else if (s->ndots >= 2 && s->dots) {
      float r = fmaxf(1.0f, s->dot * zoom);
      for (int k = 0; k + 1 < s->ndots; k += 2) {
        float px = hw + (s->dots[k] - vx) * zoom;
        float py = hh + (s->dots[k + 1] - vy) * zoom;
        if (px < -r || py < -r || px > 2 * hw + r || py > 2 * hh + r) continue;
        ImDrawList_AddCircleFilled(draw, (ImVec2){px, py}, r, col, r > 6 ? 16 : 8);
      }
    }
  }
}

static bool s_font_is_bold;

static void draw_glyph(ImDrawList* draw, ImFont* font, float size, float cx,
                       float cy, float rot, ImU32 col, const char* g,
                       const char* g_end) {
  ImVec2 sz;
  ImFont_CalcTextSizeA(&sz, font, size, FLT_MAX, 0.0f, g, g_end, NULL);
  ImVec2 pos = {cx - sz.x * 0.5f, cy - sz.y * 0.5f};
  int v0 = draw->VtxBuffer.Size;
  float bold = fmaxf(0.5f, size * 0.03f);
  ImDrawList_AddText_FontPtr(draw, font, size, pos, col, g, g_end, 0.0f, NULL);
  if (!s_font_is_bold)
    ImDrawList_AddText_FontPtr(draw, font, size, (ImVec2){pos.x + bold, pos.y},
                               col, g, g_end, 0.0f, NULL);
  if (rot != 0.0f) {
    float cs = cosf(rot), sn = sinf(rot);
    for (int i = v0; i < draw->VtxBuffer.Size; ++i) {
      ImDrawVert* v = &draw->VtxBuffer.Data[i];
      float dx = v->pos.x - cx, dy = v->pos.y - cy;
      v->pos.x = cx + dx * cs - dy * sn;
      v->pos.y = cy + dx * sn + dy * cs;
    }
  }
}

static void draw_one_text(ImDrawList* draw, ImFont* font, const live_text* t,
                          float vx, float vy, float zoom, float hw, float hh) {
  float size = 10.0f * t->cell * zoom;
  if (size < 3.0f) return;
  if (size > 1500.0f) size = 1500.0f;
  ImU32 col = pal_col(t->cv, CLEAN_ALPHA);

  const char* p = t->text;
  int count = 0;
  for (const char* q = p; *q; ++q)
    if (((unsigned char)*q & 0xC0) != 0x80) count++;

  float a0 = 0;
  if (t->has_arc)
    a0 = t->arc_mid - (count * 6 - 1) * t->cell / t->arc_r / 2.0f;

  int i = 0;
  while (*p) {
    const char* g = p;
    p++;
    while (((unsigned char)*p & 0xC0) == 0x80) p++;
    float wx, wy, rot = 0;
    if (t->has_arc) {
      float a = a0 + (i * 6 + 2) * t->cell / t->arc_r;
      wx = t->arc_cx + t->arc_r * cosf(a);
      wy = t->arc_cy + t->arc_r * sinf(a);
      rot = a + 1.57079632679f;
    } else {
      wx = t->x + (i * 6 + 2) * t->cell;
      wy = t->y + 3 * t->cell;
    }
    float sx = hw + (wx - vx) * zoom, sy = hh + (wy - vy) * zoom;
    if (!(sx < -size || sy < -size || sx > 2 * hw + size || sy > 2 * hh + size) &&
        !isspace((unsigned char)*g))
      draw_glyph(draw, font, size, sx, sy, rot, col, g, p);
    ++i;
  }
}

static void draw_texts(ImDrawList* draw, ImFont* font, float vx, float vy,
                       float zoom, float hw, float hh, int my_id) {
  if (!font) return;
  bool has_viewer_match = false;
  if (my_id >= 0) {
    for (int i = 0; i < S.frame.nt && !has_viewer_match; ++i) {
      const live_text* t = &S.frame.texts[i];
      if (!t->is_viewer_text) continue;
      for (int r = 0; r < t->nrecip; ++r)
        if (t->recip[r] == my_id) { has_viewer_match = true; break; }
    }
  }
  for (int i = 0; i < S.frame.nt; ++i) {
    const live_text* t = &S.frame.texts[i];
    if (t->is_viewer_text) {
      bool sees = false;
      if (my_id >= 0)
        for (int r = 0; r < t->nrecip; ++r)
          if (t->recip[r] == my_id) { sees = true; break; }
      if (!sees) continue;
    } else if (has_viewer_match && t->viewer_specific) {
      continue;
    }
    draw_one_text(draw, font, t, vx, vy, zoom, hw, hh);
  }
}

#define FB_WINDOW 48.0f
#define FB_MAX_DOTS 40000
#define FB_MAX_PRIMS 120000

static int fb_pt_cmp(const void* a, const void* b) {
  float d = ((const fb_pt*)a)->x - ((const fb_pt*)b)->x;
  return d < 0 ? -1 : d > 0;
}

static void fb_add(int k, fb_prim q) {
  if (FB[k].n >= FB_MAX_PRIMS) return;
  if (FB[k].n >= FB[k].cap) {
    int nc = FB[k].cap ? FB[k].cap * 2 : 2048;
    fb_prim* np = (fb_prim*)realloc(FB[k].p, sizeof(fb_prim) * nc);
    if (!np) return;
    FB[k].p = np;
    FB[k].cap = nc;
  }
  FB[k].p[FB[k].n++] = q;
}

static void fb_build(int kind) {
  int k = kind - 1;
  FB[k].n = 0;
  FB[k].built = true;
  FB[k].ver = S.dots.ver;
  FB[k].t = mg_millis() / 1000.0;
  int cnt = 0;
  for (int i = 0; i < S.dots.n; ++i)
    if (S.dots.d[i].kind == kind) cnt++;
  if (cnt == 0) return;
  if (cnt > FB_MAX_DOTS) cnt = FB_MAX_DOTS;
  fb_pt* pts = (fb_pt*)malloc(sizeof(fb_pt) * cnt);
  if (!pts) return;
  int n = 0;
  for (int i = 0; i < S.dots.n && n < cnt; ++i) {
    const live_dot* d = &S.dots.d[i];
    if (d->kind == kind) pts[n++] = (fb_pt){(float)d->x, (float)d->y, FLT_MAX, d->cv};
  }
  qsort(pts, n, sizeof(fb_pt), fb_pt_cmp);
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n && pts[j].x - pts[i].x <= FB_WINDOW; ++j) {
      float dx = pts[j].x - pts[i].x, dy = pts[j].y - pts[i].y;
      float d = sqrtf(dx * dx + dy * dy);
      if (d < pts[i].nd) pts[i].nd = d;
      if (d < pts[j].nd) pts[j].nd = d;
    }
  float wk = kind == LIVE_DOT_TEXT ? 0.62f : 0.40f;
  for (int i = 0; i < n; ++i) {
    float nd = pts[i].nd < FLT_MAX ? pts[i].nd : 4.0f;
    float w = fmaxf(1.5f, fminf(nd * wk, 14.0f));
    fb_add(k, (fb_prim){pts[i].x, pts[i].y, pts[i].x, pts[i].y, w, pts[i].cv});
    for (int j = i + 1; j < n && pts[j].x - pts[i].x <= FB_WINDOW; ++j) {
      float dx = pts[j].x - pts[i].x, dy = pts[j].y - pts[i].y;
      float d = sqrtf(dx * dx + dy * dy);
      float lim = 1.5f * fmaxf(pts[i].nd, pts[j].nd < FLT_MAX ? pts[j].nd : 0.0f);
      if (d <= lim && d > 0.0f)
        fb_add(k, (fb_prim){pts[i].x, pts[i].y, pts[j].x, pts[j].y, w, pts[i].cv});
    }
  }
  free(pts);
}

static void draw_dot_fallback(ImDrawList* draw, int kind, float vx, float vy,
                              float zoom, float hw, float hh) {
  int k = kind - 1;
  if (S.dots.count == 0) return;
  double now = mg_millis() / 1000.0;
  if (!FB[k].built || (FB[k].ver != S.dots.ver && now - FB[k].t > 0.25)) fb_build(kind);
  for (int i = 0; i < FB[k].n; ++i) {
    const fb_prim* q = &FB[k].p[i];
    float x0 = hw + (q->x0 - vx) * zoom, y0 = hh + (q->y0 - vy) * zoom;
    float x1 = hw + (q->x1 - vx) * zoom, y1 = hh + (q->y1 - vy) * zoom;
    float w = fmaxf(1.0f, q->w * zoom);
    if (fmaxf(x0, x1) < -w || fminf(x0, x1) > 2 * hw + w ||
        fmaxf(y0, y1) < -w || fminf(y0, y1) > 2 * hh + w)
      continue;
    ImU32 col = pal_col(q->cv, CLEAN_ALPHA);
    if (q->x0 == q->x1 && q->y0 == q->y1)
      ImDrawList_AddCircleFilled(draw, (ImVec2){x0, y0}, w * 0.5f, col, w > 8 ? 16 : 8);
    else
      ImDrawList_AddLine(draw, (ImVec2){x0, y0}, (ImVec2){x1, y1}, col, w);
  }
}

static void fb_free(void) {
  for (int k = 0; k < 2; ++k) { free(FB[k].p); memset(&FB[k], 0, sizeof FB[k]); }
}

void live_feed_draw(tenv* env, float view_x, float view_y, float zoom,
                    float half_w, float half_h, int my_snake_id,
                    bool draw_lines, bool draw_text) {
  if (!live_feed_active() || zoom <= 0) return;
  if (!draw_lines && !draw_text) return;
  ImDrawList* draw = igGetWindowDrawList();
  if (!draw) return;
  if (draw_lines) {
    if (S.frame.ns > 0) draw_shapes(draw, view_x, view_y, zoom, half_w, half_h);
    else draw_dot_fallback(draw, LIVE_DOT_SHAPE, view_x, view_y, zoom, half_w, half_h);
  }
  if (draw_text && S.frame.nt == 0) {
    draw_dot_fallback(draw, LIVE_DOT_TEXT, view_x, view_y, zoom, half_w, half_h);
  } else if (draw_text) {

    ImFont* font = NULL;
    if (env && env->usr) font = env->usr->imgui_data.mono_font_bold[FONT_SIZE_REGULAR];
    s_font_is_bold = font != NULL;
    if (!font) font = igGetFont();
    draw_texts(draw, font, view_x, view_y, zoom, half_w, half_h, my_snake_id);
  }
}
