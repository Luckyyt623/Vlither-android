#include "tag_cord.h"

#include <math.h>

#define TAG_CORD_LEN 3.15f    // cord length = this * body radius
#define TAG_CORD_STEP 0.25f   // trail point spacing (body radii)
#define TAG_CORD_START 0.5f   // cord starts this far back from head (body radii)
#define TAG_CORD_WIDE 0.275f  // string thickness = this * body radius
#define TAG_TRAIL_MAX 128
#define TAG_STATE_MAX 192

typedef struct {
  int id;
  unsigned last_use;
  float x[TAG_TRAIL_MAX], y[TAG_TRAIL_MAX];  // newest point at n-1
  int n;
  float fe[2];     // eased tag angle per slot
  bool init[2];
} tag_cord_state;

static tag_cord_state g_state[TAG_STATE_MAX];
static int g_state_n = 0;
static unsigned g_tick = 0;

static tag_cord_state *state_get(int id) {
  g_tick++;
  for (int i = 0; i < g_state_n; i++)
    if (g_state[i].id == id) {
      g_state[i].last_use = g_tick;
      return &g_state[i];
    }
  int idx;
  if (g_state_n < TAG_STATE_MAX) {
    idx = g_state_n++;
  } else {  // full -> recycle least recently used
    idx = 0;
    for (int i = 1; i < TAG_STATE_MAX; i++)
      if (g_state[i].last_use < g_state[idx].last_use) idx = i;
  }
  tag_cord_state *s = &g_state[idx];
  s->id = id;
  s->last_use = g_tick;
  s->n = 0;
  s->init[0] = s->init[1] = false;
  s->fe[0] = s->fe[1] = 0.0f;
  return s;
}

static void trail_push(tag_cord_state *t, float x, float y) {
  if (t->n >= TAG_TRAIL_MAX) {  // drop oldest
    for (int i = 1; i < TAG_TRAIL_MAX; i++) {
      t->x[i - 1] = t->x[i];
      t->y[i - 1] = t->y[i];
    }
    t->n = TAG_TRAIL_MAX - 1;
  }
  t->x[t->n] = x;
  t->y[t->n] = y;
  t->n++;
}

// lay down evenly-spaced trail points up to the head; reset on teleport
static void trail_update(tag_cord_state *t, float hx, float hy, float step) {
  if (step < 2.0f) step = 2.0f;
  if (t->n == 0) {
    trail_push(t, hx, hy);
    return;
  }
  float lx = t->x[t->n - 1], ly = t->y[t->n - 1];
  float dx = hx - lx, dy = hy - ly;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > step * 40.0f) {  // teleport (respawn) -> restart trail
    t->n = 0;
    trail_push(t, hx, hy);
    return;
  }
  int guard = 0;
  while (d >= step && guard++ < TAG_TRAIL_MAX) {
    lx += dx / d * step;
    ly += dy / d * step;
    trail_push(t, lx, ly);
    dx = hx - lx;
    dy = hy - ly;
    d = sqrtf(dx * dx + dy * dy);
  }
}

static float ease_angle(tag_cord_state *s, int slot, float target) {
  if (!s->init[slot]) {
    s->fe[slot] = target;
    s->init[slot] = true;
    return target;
  }
  float d = target - s->fe[slot];
  while (d > 3.14159265f) d -= 6.2831853f;
  while (d < -3.14159265f) d += 6.2831853f;
  s->fe[slot] += 0.15f * d;
  return s->fe[slot];
}

static void apply_fan(tag_cord *c, float fan_px) {
  if (fan_px == 0.0f || c->np < 2) return;
  float dx = c->pls[c->np - 1].x - c->pls[0].x;
  float dy = c->pls[c->np - 1].y - c->pls[0].y;
  float l = sqrtf(dx * dx + dy * dy);
  if (l < 0.0001f) return;
  float px = -dy / l, py = dx / l;
  for (int i = 1; i < c->np; i++) {
    float f = (float)i / (float)(c->np - 1);
    c->pls[i].x += px * fan_px * f;
    c->pls[i].y += py * fan_px * f;
  }
}

void tag_cord_build(tag_cord *c, int snake_id, int slot, float hx, float hy,
                    float rad, float head_ang, const tag_cord_view *v,
                    float fan_px) {
  slot = slot ? 1 : 0;
  c->np = 0;
  c->wide = rad * TAG_CORD_WIDE * v->gsc;
  float clen = rad * TAG_CORD_LEN;
  float start = TAG_CORD_START * rad;
  tag_cord_state *st = state_get(snake_id);
  trail_update(st, hx, hy, rad * TAG_CORD_STEP);

#define W2S_X(wx) (v->mww2 + ((wx) - v->view_xx) * v->gsc)
#define W2S_Y(wy) (v->mhh2 + ((wy) - v->view_yy) * v->gsc)

  if (st->n >= 2) {
    float ax = hx, ay = hy, acc = 0.0f, prevx = hx, prevy = hy;
    bool started = false, got = false;
    for (int i = st->n - 1; i >= 0 && c->np < TAG_CORD_MAX_POINTS - 1; i--) {
      float cx = st->x[i], cy = st->y[i];
      float dx = cx - prevx, dy = cy - prevy;
      float seg = sqrtf(dx * dx + dy * dy);
      if (!started && acc + seg >= start) {  // begin the cord on the neck
        float f = seg > 0.001f ? (start - acc) / seg : 0.0f;
        c->pls[c->np++] = (ImVec2){W2S_X(prevx + dx * f), W2S_Y(prevy + dy * f)};
        started = true;
      }
      if (acc + seg >= clen) {
        float f = seg > 0.001f ? (clen - acc) / seg : 0.0f;
        ax = prevx + dx * f;
        ay = prevy + dy * f;
        if (c->np < TAG_CORD_MAX_POINTS)
          c->pls[c->np++] = (ImVec2){W2S_X(ax), W2S_Y(ay)};
        got = true;
        break;
      }
      acc += seg;
      if (started && c->np < TAG_CORD_MAX_POINTS - 1)
        c->pls[c->np++] = (ImVec2){W2S_X(cx), W2S_Y(cy)};
      prevx = cx;
      prevy = cy;
    }
    if (!got) {
      ax = prevx;
      ay = prevy;
    }
    if (c->np == 0)  // trail shorter than start offset (just spawned)
      c->pls[c->np++] = (ImVec2){W2S_X(ax), W2S_Y(ay)};
  }
  if (c->np < 2) {  // no trail yet -> straight cord straight back from head
    float bx = -cosf(head_ang), by = -sinf(head_ang);
    c->pls[0] = (ImVec2){W2S_X(hx + bx * start), W2S_Y(hy + by * start)};
    c->pls[1] = (ImVec2){W2S_X(hx + bx * clen), W2S_Y(hy + by * clen)};
    c->np = 2;
  }
#undef W2S_X
#undef W2S_Y

  float raw = head_ang + 3.14159265f;
  float ddx = c->pls[c->np - 1].x - c->pls[0].x;
  float ddy = c->pls[c->np - 1].y - c->pls[0].y;
  if (ddx * ddx + ddy * ddy > 0.0001f) raw = atan2f(ddy, ddx);
  c->ang = ease_angle(st, slot, raw);
  apply_fan(c, fan_px);
}

void tag_cord_build_straight(tag_cord *c, float head_x, float head_y,
                             float rad_px, float head_ang) {
  float bx = -cosf(head_ang), by = -sinf(head_ang);
  float start = TAG_CORD_START * rad_px, clen = TAG_CORD_LEN * rad_px;
  c->np = 2;
  c->pls[0] = (ImVec2){head_x + bx * start, head_y + by * start};
  c->pls[1] = (ImVec2){head_x + bx * clen, head_y + by * clen};
  c->ang = head_ang + 3.14159265f;
  c->wide = rad_px * TAG_CORD_WIDE;
}

void tag_cord_draw(ImDrawList *dl, const tag_cord *c, const float mc[3],
                   const float ec[3], float a) {
  if (!dl || !c || c->np < 2) return;
  float wide = c->wide < 2.5f ? 2.5f : c->wide;
  const ImVec2 *pls = c->pls;
  int np = c->np;

  // faded black halo
  const float bw[3] = {wide * 1.55f, wide * 1.35f, wide * 1.18f};
  const float bal[3] = {0.10f, 0.24f, 0.42f};
  for (int b = 0; b < 3; b++) {
    ImU32 col = igColorConvertFloat4ToU32((ImVec4){0, 0, 0, bal[b] * a});
    ImDrawList_AddPolyline(dl, pls, np, col, 0, bw[b]);
    ImDrawList_AddCircleFilled(dl, pls[0], bw[b] * 0.5f, col, 16);
    ImDrawList_AddCircleFilled(dl, pls[np - 1], bw[b] * 0.5f, col, 16);
  }
  // smooth rim -> core colour gradient
  const int GL = 7;
  for (int L = 0; L < GL; L++) {
    float t = (float)L / (float)(GL - 1);
    float lw = wide * (1.0f - 0.55f * t);
    float r = ec[0] + (mc[0] - ec[0]) * t;
    float g = ec[1] + (mc[1] - ec[1]) * t;
    float b2 = ec[2] + (mc[2] - ec[2]) * t;
    float al = (0.5f + 0.45f * t) * a;
    ImU32 col = igColorConvertFloat4ToU32((ImVec4){r, g, b2, al});
    ImDrawList_AddPolyline(dl, pls, np, col, 0, lw);
    ImDrawList_AddCircleFilled(dl, pls[0], lw * 0.5f, col, 16);
    ImDrawList_AddCircleFilled(dl, pls[np - 1], lw * 0.5f, col, 16);
  }
}
