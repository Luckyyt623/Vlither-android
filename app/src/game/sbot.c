#include "sbot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

sbot_dbg g_bot_dbg;

#include "../user.h"

/* ---- NTL MOD parity constants (taken from NTL MOD v9.68 bot) ------------ */
#define ARC_SIZE (M_PI / 8.0f)
#define MAXARC 16
#define SPEED_BASE 5.78f
#define FRONT_ANGLE (M_PI / 2.0f)
#define ENCIRCLE_THRESH 0.5f
#define ENCIRCLE_ALL_THRESH 0.5f
#define ENCIRCLE_DIST_MULT 30.0f
#define NTL_RADIUS_MULT 12 /* NTL hard-codes 12; Vlither used the user setting */
#define SNAKE_WIDTH_MULT 29.2f
#define NEARBY_BOX 2400.0f /* enemy body points are only considered in this box */

/* Wall: NTL treats a ring at 0.98 * arena radius as the wall (2% early
   margin), sampled as 251 points of radius 8 spaced 8 units apart (+-1000
   around the snake), whenever the head is within 3500 of the border. */
#define WALL_RANGE 3500.0f
#define WALL_RING_SCALE 0.98f
#define WALL_PT_RADIUS 8.0f
#define WALL_HALF_SPAN 1000
#define WALL_STEP 8
#define WALL_GROUP 511 /* encircle bucket for wall points (never a snake) */

/* Coil tuning (NTL values) */
#define COIL_WALL_BIAS_RANGE 1650.0f
#define ENEMY_BODY_MULT 0.38f
#define ENEMY_BODY_OD0 0.15f
#define WALL_OFF_MULT 0.22f
#define WALL_OD0 0.25f
#define COIL_MIN_COURSE (-0.8f)
#define COIL_BODY_PENALTY 0.16f
#define TO_CIRCLE_I8 25.0f
#define SELF_CHECK_FRAC 0.85f

/* NTL timing: bot ticks at 25 Hz; retarget after 5 ticks, back off 25 ticks
   after a collision decision. */
#define ACTION_MS 200.0
#define COLLISION_MS 1000.0

/* Vlither's extra predictive border override. NTL does not have it (its dense
   0.98 ring does the job), so it is off for exact NTL behaviour. */
#ifndef SBOT_BORDER_EMERGENCY
#define SBOT_BORDER_EMERGENCY 0
#endif

#define MAX_BODY_PTS 8192
#define MAX_HULL_PTS 32
#define MAX_INSIDE_PTS 512
#define MAX_COLL_PTS 1024

typedef struct {
  float x, y;
} v2;
typedef struct {
  float x, y, r;
} circ;
typedef struct {
  float x, y, a;
} isect;

typedef struct {
  v2 pts[MAX_HULL_PTS];
  int n;
  float minx, maxx, miny, maxy;
} poly_box;

typedef struct {
  v2 pts[MAX_INSIDE_PTS];
  int n;
  float minx, maxx, miny, maxy;
} poly_box_large;

typedef struct {
  float x, y, d2, r;
  int si, type;
} coll_pt;
typedef struct {
  float x, y, ang, d2, r;
  int si, aIndex;
} coll_ang;
typedef struct {
  float x, y, ang, da, d2, sz, score;
} food_ang;
typedef struct {
  float x, y, len;
} body_pt;

typedef struct {
  int stage;
  bool timer_pending;
  double timer_at;
  int default_accel;
  int circle_dir;
  bool enable_encircle;
  bool enable_follow_circle;

  int id;
  float x, y, ang, ca, sa;
  float speed_mult, width, radius;
  int snake_len;

  circ head_circle, side_l, side_r;

  coll_ang coll_angles[MAXARC];
  bool coll_angles_set[MAXARC];
  food_ang food_angles[MAXARC];
  bool food_angles_set[MAXARC];

  food_ang current_food;
  bool has_food;

  coll_pt coll_pts[MAX_COLL_PTS];
  int coll_pts_n;

  v2 goal;

  body_pt bpts[MAX_BODY_PTS];
  int bpts_n;
  float bpts_len;

  float radius_mult;
  float follow_circle_length;

  /* wall info, refreshed every frame in every() */
  bool near_wall;       /* within WALL_RANGE of the (possibly shrunk) border */
  float wall_dist;      /* |edge - distance from centre| */
  float wall_ring_r;    /* radius of the sampled wall ring */
  float wall_view_ang;  /* angle from arena centre to the head */
  float wall_dist_grd;  /* |grd - distance from centre| (ignores shrink) */
  float follow_tx, follow_ty; /* NTL followCircleTarget */
} bot_state;

static bot_state B;

static inline float dist2(float ax, float ay, float bx, float by) {
  float dx = ax - bx, dy = ay - by;
  return dx * dx + dy * dy;
}

static inline float safe_border_radius(const game_data* gdata) {
  float smoothed = gdata->data.flux_grd;
  float server = gdata->data.real_flux_grd;
  /* During a server shrink the renderer eases flux_grd over several frames,
     but collision authority has already moved to real_flux_grd. The bot must
     obey whichever boundary is smaller. */
  if (server > 0.0f && (smoothed <= 0.0f || server < smoothed)) return server;
  return smoothed;
}

static inline float ang_between(float a, float b) {
  float r1 = fmodf(a - b, (float)M_PI);
  float r2 = fmodf(b - a, (float)M_PI);
  return r1 < r2 ? -r1 : r2;
}

static inline int ang_index(float angle) {
  float a = angle < 0 ? angle + PI2 : angle;
  int i = (int)roundf(a * (1.0f / ARC_SIZE));
  return i == MAXARC ? 0 : i;
}

static inline float snake_width(float sc) {
  return roundf(sc * SNAKE_WIDTH_MULT);
}

static inline v2 unit_vec(v2 v) {
  float l = sqrtf(v.x * v.x + v.y * v.y);
  return l > 0 ? (v2){v.x / l, v.y / l} : (v2){0, 0};
}

static inline float cross2(v2 o, v2 a, v2 b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static int hull_cmp(const void* a, const void* b) {
  v2 pa = *(v2*)a, pb = *(v2*)b;
  if (pa.x != pb.x) return pa.x < pb.x ? -1 : 1;
  return pa.y < pb.y ? -1 : pa.y > pb.y ? 1 : 0;
}

static int convex_hull(v2* pts, int n, v2* out) {
  if (n < 2) {
    for (int i = 0; i < n; i++) out[i] = pts[i];
    return n;
  }
  qsort(pts, n, sizeof(v2), hull_cmp);

  v2 stk[MAX_HULL_PTS * 2];
  int k = 0;

  for (int i = 0; i < n; i++) {
    while (k >= 2 && cross2(stk[k - 2], stk[k - 1], pts[i]) <= 0) k--;
    stk[k++] = pts[i];
  }

  int lo = k + 1;
  for (int i = n - 2; i >= 0; i--) {
    while (k >= lo && cross2(stk[k - 2], stk[k - 1], pts[i]) <= 0) k--;
    stk[k++] = pts[i];
  }
  k--;
  for (int i = 0; i < k; i++) out[i] = stk[i];
  return k;
}

static bool point_in_poly(v2 p, const poly_box* pb) {
  if (p.x < pb->minx || p.x > pb->maxx || p.y < pb->miny || p.y > pb->maxy)
    return false;
  bool c = false;
  int l = pb->n;
  for (int i = 0, j = l - 1; i < l; j = i++) {
    if ((pb->pts[i].y > p.y) != (pb->pts[j].y > p.y) &&
        p.x < (pb->pts[j].x - pb->pts[i].x) * (p.y - pb->pts[i].y) /
                      (pb->pts[j].y - pb->pts[i].y) +
                  pb->pts[i].x)
      c = !c;
  }
  return c;
}

static bool point_in_poly_large(v2 p, const poly_box_large* pb) {
  if (p.x < pb->minx || p.x > pb->maxx || p.y < pb->miny || p.y > pb->maxy)
    return false;
  bool c = false;
  int l = pb->n;
  for (int i = 0, j = l - 1; i < l; j = i++) {
    if ((pb->pts[i].y > p.y) != (pb->pts[j].y > p.y) &&
        p.x < (pb->pts[j].x - pb->pts[i].x) * (p.y - pb->pts[i].y) /
                      (pb->pts[j].y - pb->pts[i].y) +
                  pb->pts[i].x)
      c = !c;
  }
  return c;
}

static void poly_box_aabb(poly_box* pb) {
  pb->minx = pb->maxx = pb->pts[0].x;
  pb->miny = pb->maxy = pb->pts[0].y;
  for (int i = 1; i < pb->n; i++) {
    if (pb->pts[i].x < pb->minx) pb->minx = pb->pts[i].x;
    if (pb->pts[i].x > pb->maxx) pb->maxx = pb->pts[i].x;
    if (pb->pts[i].y < pb->miny) pb->miny = pb->pts[i].y;
    if (pb->pts[i].y > pb->maxy) pb->maxy = pb->pts[i].y;
  }
}

static void poly_box_large_aabb(poly_box_large* pb) {
  pb->minx = pb->maxx = pb->pts[0].x;
  pb->miny = pb->maxy = pb->pts[0].y;
  for (int i = 1; i < pb->n; i++) {
    if (pb->pts[i].x < pb->minx) pb->minx = pb->pts[i].x;
    if (pb->pts[i].x > pb->maxx) pb->maxx = pb->pts[i].x;
    if (pb->pts[i].y < pb->miny) pb->miny = pb->pts[i].y;
    if (pb->pts[i].y > pb->maxy) pb->maxy = pb->pts[i].y;
  }
}

static bool circle_intersect(circ c1, circ c2, isect* out) {
  float br = c1.r + c2.r;
  if (c1.x + br <= c2.x || c2.x + br <= c1.x) return false;
  if (c1.y + br <= c2.y || c2.y + br <= c1.y) return false;
  if (dist2(c1.x, c1.y, c2.x, c2.y) >= br * br) return false;
  if (out) {
    out->x = (c1.x * c2.r + c2.x * c1.r) / br;
    out->y = (c1.y * c2.r + c2.y * c1.r) / br;
    out->a = atan2f(out->y - B.y, out->x - B.x);
  }
  return true;
}

static v2 heading_abs(float angle) {
  /* NTL: He() aims at head-circle radius; keep a floor so the mouse vector
     never falls inside the game's dead-zone. Only the direction matters. */
  float r = fmaxf(B.head_circle.r, 100.0f);
  return (v2){roundf(B.x + r * cosf(angle)), roundf(B.y + r * sinf(angle))};
}

static v2 heading_rel(float angle) {
  float hx = B.x + 500.0f * B.ca, hy = B.y + 500.0f * B.sa;
  float c = cosf(-angle), s = sinf(-angle);
  return (v2){roundf(c * (hx - B.x) - s * (hy - B.y) + B.x),
              roundf(s * (hx - B.x) + c * (hy - B.y) + B.y)};
}

static inline bool is_left(v2 start, v2 end, v2 p) {
  return (end.x - start.x) * (p.y - start.y) -
             (end.y - start.y) * (p.x - start.x) >
         0;
}

static void add_coll_angle(coll_pt* sp) {
  float ang = atan2f(roundf(sp->y - B.y), roundf(sp->x - B.x));
  int ai = ang_index(ang);
  float adj = sqrtf(sp->d2) - sp->r;
  float ad2 = roundf(adj * adj);
  if (!B.coll_angles_set[ai] || B.coll_angles[ai].d2 > ad2) {
    B.coll_angles[ai] =
        (coll_ang){roundf(sp->x), roundf(sp->y), ang, ad2, sp->r, sp->si, ai};
    B.coll_angles_set[ai] = true;
  }
}

static void add_food_angle(float fx, float fy, float fd2, float fsz) {
  float ang = atan2f(roundf(fy - B.y), roundf(fx - B.x));
  int ai = ang_index(ang);
  if (B.coll_angles_set[ai]) {
    float cd = sqrtf(B.coll_angles[ai].d2);
    float fd = sqrtf(fd2);
    if (cd <= fd + (B.radius * B.radius_mult * B.speed_mult) / 2.0f) return;
  }
  float score = fsz * fsz / fd2;
  if (!B.food_angles_set[ai]) {
    B.food_angles[ai] =
        (food_ang){roundf(fx), roundf(fy), ang,  fabsf(ang_between(ang, B.ang)),
                   fd2,        fsz,        score};
    B.food_angles_set[ai] = true;
  } else {
    food_ang* fa = &B.food_angles[ai];
    fa->sz += roundf(fsz);
    fa->score += score;
    if (fa->d2 > fd2) {
      fa->x = roundf(fx);
      fa->y = roundf(fy);
      fa->d2 = fd2;
    }
  }
}

static int coll_pt_cmp(const void* a, const void* b) {
  float da = ((coll_pt*)a)->d2, db = ((coll_pt*)b)->d2;
  return (da > db) - (da < db);
}

static void get_collision_points(game_data* gdata) {
  B.coll_pts_n = 0;
  memset(B.coll_angles_set, 0, sizeof(B.coll_angles_set));

  int ns = tdarray_length(gdata->data.snakes);

  for (int i = 0; i < ns; i++) {
    snake* s = gdata->data.snakes + i;
    if (s->id == B.id || s->dead) continue;

    float sr = snake_width(s->sc) / 2.0f;
    float smul = fminf(1.0f, s->sp / SPEED_BASE - 1.0f);
    float srm = sr * smul * B.radius_mult;

    float hx = s->xx + cosf(s->ang) * srm / 2.0f;
    float hy = s->yy + sinf(s->ang) * srm / 2.0f;
    float hd2 = dist2(B.x, B.y, hx, hy);

    coll_pt head = {hx, hy, hd2, B.head_circle.r, i, 0};
    add_coll_angle(&head);
    if (B.coll_pts_n < MAX_COLL_PTS) B.coll_pts[B.coll_pts_n++] = head;

    int pn = tdarray_length(s->pts);
    for (int j = 0; j < pn; j++) {
      body_part* po = s->pts + j;
      if (po->dying) continue;
      /* NTL: only body points inside a 2400x2400 box around the head. */
      if (fabsf(po->xx - B.x) > NEARBY_BOX * 0.5f ||
          fabsf(po->yy - B.y) > NEARBY_BOX * 0.5f)
        continue;
      float pd2 = dist2(B.x, B.y, po->xx, po->yy);
      coll_pt bp = {po->xx, po->yy, pd2, sr, i, 1};
      add_coll_angle(&bp);
      float tr = B.head_circle.r + sr;
      if (pd2 <= tr * tr && B.coll_pts_n < MAX_COLL_PTS)
        B.coll_pts[B.coll_pts_n++] = bp;
    }
  }

  /* NTL wall: 251 points (r=8, step 8, +-1000) on a ring at 0.98 * arena
     radius (or the live shrunk radius), only within 3500 of the border. */
  if (B.near_wall && gdata->data.grd > 0.0f) {
    const float grd = gdata->data.grd;
    for (int k = -WALL_HALF_SPAN; k <= WALL_HALF_SPAN; k += WALL_STEP) {
      float wa = B.wall_view_ang + (float)k / grd;
      float wx = grd + cosf(wa) * B.wall_ring_r;
      float wy = grd + sinf(wa) * B.wall_ring_r;
      float wd2 = dist2(B.x, B.y, wx, wy);
      coll_pt wp = {wx, wy, wd2, WALL_PT_RADIUS, WALL_GROUP, 2};
      add_coll_angle(&wp);
      if (B.coll_pts_n < MAX_COLL_PTS) B.coll_pts[B.coll_pts_n++] = wp;
    }
  }

  qsort(B.coll_pts, B.coll_pts_n, sizeof(coll_pt), coll_pt_cmp);
}

static bool in_front(float px, float py) {
  float ang = atan2f(roundf(py - B.y), roundf(px - B.x));
  return fabsf(ang_between(ang, B.ang)) < FRONT_ANGLE;
}

static void avoid_point(isect* ip) {
  v2 head = {B.x, B.y};
  v2 end = {B.x + 2000.0f * B.ca, B.y + 2000.0f * B.sa};
  v2 pt = {ip->x, ip->y};
  float new_ang =
      is_left(head, end, pt) ? ip->a - (float)M_PI : ip->a + (float)M_PI;
  B.goal = heading_abs(new_ang);

  g_bot_dbg.avoid_active  = true;
  g_bot_dbg.avoid_ix      = ip->x;
  g_bot_dbg.avoid_iy      = ip->y;
  g_bot_dbg.avoid_fwd_x   = end.x;
  g_bot_dbg.avoid_fwd_y   = end.y;
}

static void heading_best_angle(void) {
  typedef struct {
    int s, e, sz;
  } oa;
  oa opens[MAXARC];
  int no = 0, open_start = -1;
  int best_ai = -1;
  float best_d = -1.0f;

  for (int i = 0; i < MAXARC; i++) {
    if (!B.coll_angles_set[i]) {
      if (open_start < 0) open_start = i;
    } else {
      float d = B.coll_angles[i].d2;
      if (best_ai < 0 || (d > best_d && d != 0.0f)) {
        best_d = d;
        best_ai = i;
      }
      if (open_start >= 0) {
        opens[no++] = (oa){open_start, i - 1, i - 1 - open_start};
        open_start = -1;
      }
    }
  }
  if (open_start >= 0) {
    if (no > 0) {
      opens[0].s = open_start;
      opens[0].sz = opens[0].e - open_start;
      if (opens[0].sz < 0) opens[0].sz += MAXARC;
    } else {
      opens[no++] = (oa){open_start, open_start, 0};
    }
  }

  if (no > 0) {
    for (int a = 0; a < no - 1; a++)
      for (int b = a + 1; b < no; b++)
        if (opens[b].sz > opens[a].sz) {
          oa t = opens[a];
          opens[a] = opens[b];
          opens[b] = t;
        }
    float mid = (opens[0].e - opens[0].sz / 2.0f) * ARC_SIZE;
    B.goal = heading_abs(mid);
  } else if (best_ai >= 0) {
    B.goal = heading_abs(best_ai * ARC_SIZE);
  }
}

static bool check_collision(game_data* gdata) {
  get_collision_points(gdata);
  if (B.coll_pts_n == 0) return false;
  for (int i = 0; i < B.coll_pts_n; i++) {
    coll_pt* cp = B.coll_pts + i;
    circ col = {cp->x, cp->y, cp->r};
    isect ip;
    if (circle_intersect(B.head_circle, col, &ip) && in_front(ip.x, ip.y)) {
      if (cp->type == 0) {
        snake* s = gdata->data.snakes + cp->si;
        gdata->bot.output.accel = s->sp > 10.0f;
      } else {
        gdata->bot.output.accel = (bool)B.default_accel;
      }
      avoid_point(&ip);
      return true;
    }
  }
  gdata->bot.output.accel = (bool)B.default_accel;
  return false;
}

static bool check_encircle(game_data* gdata) {
  if (!B.enable_encircle) return false;
  int en[512];
  memset(en, 0, sizeof(en));
  int high = 0, high_si = 0, en_all = 0;
  for (int i = 0; i < MAXARC; i++) {
    if (!B.coll_angles_set[i]) continue;
    coll_ang* ca = &B.coll_angles[i];
    if (ca->si >= 0 && ca->si < 512) {
      if (++en[ca->si] > high) {
        high = en[ca->si];
        high_si = ca->si;
      }
    }
    float ed = B.radius * ENCIRCLE_DIST_MULT;
    if (ca->d2 < ed * ed) en_all++;
  }
  if (high > (int)(MAXARC * ENCIRCLE_THRESH)) {
    heading_best_angle();
    int ns = tdarray_length(gdata->data.snakes);
    if (high != MAXARC && high_si != WALL_GROUP && high_si < ns)
      gdata->bot.output.accel = gdata->data.snakes[high_si].sp > 10.0f;
    else
      gdata->bot.output.accel = (bool)B.default_accel;
    g_bot_dbg.encircle_state = 1;
    return true;
  }
  if (en_all > (int)(MAXARC * ENCIRCLE_ALL_THRESH)) {
    heading_best_angle();
    gdata->bot.output.accel = (bool)B.default_accel;
    g_bot_dbg.encircle_state = 2;
    return true;
  }
  gdata->bot.output.accel = (bool)B.default_accel;
  g_bot_dbg.encircle_state = 0;
  return false;
}

static void populate_pts(game_data* gdata) {
  B.bpts_n = 0;
  B.bpts_len = 0.0f;
  int ns = tdarray_length(gdata->data.snakes);
  snake* me = NULL;
  for (int i = 0; i < ns; i++)
    if (gdata->data.snakes[i].id == B.id) {
      me = gdata->data.snakes + i;
      break;
    }
  if (!me) return;

  B.bpts[B.bpts_n++] = (body_pt){B.x, B.y, 0.0f};
  float lx = B.x, ly = B.y, l = 0.0f;
  int pn = tdarray_length(me->pts);

  for (int p = pn - 1; p >= 0 && B.bpts_n < MAX_BODY_PTS; p--) {
    body_part* po = me->pts + p;
    if (po->dying) continue;
    l += sqrtf(dist2(lx, ly, po->xx, po->yy));
    B.bpts[B.bpts_n++] = (body_pt){po->xx, po->yy, l};
    lx = po->xx;
    ly = po->yy;
  }
  B.bpts_len = l;
}

static v2 smooth_point(float t) {
  if (t <= 0.0f) return (v2){B.bpts[0].x, B.bpts[0].y};
  if (t >= B.bpts_len) {
    int e = B.bpts_n - 1;
    return (v2){B.bpts[e].x, B.bpts[e].y};
  }
  int lo = 0, hi = B.bpts_n - 1;
  while (hi - lo > 1) {
    int m = (lo + hi) / 2;
    if (t > B.bpts[m].len)
      lo = m;
    else
      hi = m;
  }
  float wp = B.bpts[hi].len - t, wq = t - B.bpts[lo].len, w = wp + wq;
  return (v2){(wp * B.bpts[lo].x + wq * B.bpts[hi].x) / w,
              (wp * B.bpts[lo].y + wq * B.bpts[hi].y) / w};
}

static float closest_body_point(void) {
  if (B.bpts_n < 3) return B.bpts_len;
  int start_n = 0;
  float start_d2 = 0.0f;
  for (;;) {
    float prev = start_d2;
    start_n++;
    if (start_n >= B.bpts_n) break;
    start_d2 = dist2(B.x, B.y, B.bpts[start_n].x, B.bpts[start_n].y);
    if (start_d2 < prev || start_n == B.bpts_n - 1) break;
  }
  if (start_n >= B.bpts_n || start_n <= 1) return B.bpts_len;

  int min_n = start_n;
  float min_d2 = start_d2;
  for (int n = min_n + 1; n < B.bpts_n; n++) {
    float d2 = dist2(B.x, B.y, B.bpts[n].x, B.bpts[n].y);
    if (d2 < min_d2) {
      min_n = n;
      min_d2 = d2;
    }
  }

  int next_n;
  float next_d2;
  if (min_n == B.bpts_n - 1) {
    next_n = min_n - 1;
    next_d2 = dist2(B.x, B.y, B.bpts[next_n].x, B.bpts[next_n].y);
  } else {
    float dm = dist2(B.x, B.y, B.bpts[min_n - 1].x, B.bpts[min_n - 1].y);
    float dp = dist2(B.x, B.y, B.bpts[min_n + 1].x, B.bpts[min_n + 1].y);
    if (dm < dp) {
      next_n = min_n - 1;
      next_d2 = dm;
    } else {
      next_n = min_n + 1;
      next_d2 = dp;
    }
  }

  float t2 = B.bpts[min_n].len - B.bpts[next_n].len;
  t2 *= t2;
  if (t2 == 0.0f) return B.bpts[min_n].len;
  float min_w = t2 - (min_d2 - next_d2);
  float next_w = t2 + (min_d2 - next_d2);
  return (B.bpts[min_n].len * min_w + B.bpts[next_n].len * next_w) /
         (2.0f * t2);
}

static void determine_circle_dir(void) {
  float cx = 0, cy = 0;
  for (int i = 0; i < B.bpts_n; i++) {
    cx += B.bpts[i].x;
    cy += B.bpts[i].y;
  }
  cx /= B.bpts_n;
  cy /= B.bpts_n;
  float dx = B.x - cx, dy = B.y - cy;
  B.circle_dir = (-dy * B.ca + dx * B.sa > 0.0f) ? -1 : +1;
}

static void body_danger_zone(float offset, v2 target_pt,
                             v2 target_norm,
                             float close_dist,
                             v2 past_target, v2 close_pt, poly_box* out) {
  int o = B.circle_dir;
  v2 raw[10];
  raw[0] = (v2){B.x - o * offset * B.sa, B.y + o * offset * B.ca};
  raw[1] = (v2){B.x + B.width * B.ca + offset * (B.ca - o * B.sa),
                B.y + B.width * B.sa + offset * (B.sa + o * B.ca)};
  raw[2] = (v2){B.x + 1.75f * B.width * B.ca + o * 0.3f * B.width * B.sa +
                    offset * (B.ca - o * B.sa),
                B.y + 1.75f * B.width * B.sa - o * 0.3f * B.width * B.ca +
                    offset * (B.sa + o * B.ca)};
  raw[3] = (v2){B.x + 2.5f * B.width * B.ca + o * 0.7f * B.width * B.sa +
                    offset * (B.ca - o * B.sa),
                B.y + 2.5f * B.width * B.sa - o * 0.7f * B.width * B.ca +
                    offset * (B.sa + o * B.ca)};
  raw[4] = (v2){
      B.x + 3.0f * B.width * B.ca + o * 1.2f * B.width * B.sa + offset * B.ca,
      B.y + 3.0f * B.width * B.sa - o * 1.2f * B.width * B.ca + offset * B.sa};
  float cpd_half = 0.5f * fmaxf(close_dist, 0.0f);
  raw[5] = (v2){target_pt.x + target_norm.x * (offset + cpd_half),
                target_pt.y + target_norm.y * (offset + cpd_half)};
  raw[6] = (v2){past_target.x + target_norm.x * offset,
                past_target.y + target_norm.y * offset};
  raw[7] = past_target;
  raw[8] = target_pt;
  raw[9] = close_pt;

  v2 scratch[10];
  memcpy(scratch, raw, sizeof(raw));
  out->n = convex_hull(scratch, 10, out->pts);
  poly_box_aabb(out);
}

static void construct_inside_polygon(float close_t, poly_box_large* out) {
  float start_t = 5.0f * B.width;
  float end_t = close_t + 5.0f * B.width;

  out->n = 0;
  out->pts[out->n++] = smooth_point(end_t);
  out->pts[out->n++] = smooth_point(start_t);
  for (float t = start_t; t < end_t && out->n < MAX_INSIDE_PTS; t += B.width)
    out->pts[out->n++] = smooth_point(t);

  poly_box_large_aabb(out);
}

static void follow_circle_self(game_data* gdata) {
  populate_pts(gdata);
  determine_circle_dir();
  int o = B.circle_dir;

  if (B.bpts_len < 9.0f * B.width) return;

  float close_t = closest_body_point();
  v2 close_pt = smooth_point(close_t);
  v2 close_next = smooth_point(close_t - B.width);
  v2 close_tang =
      unit_vec((v2){close_next.x - close_pt.x, close_next.y - close_pt.y});
  v2 close_norm = (v2){-o * close_tang.y, o * close_tang.x};

  float cur_course = asinf(
      fmaxf(-1.0f, fminf(1.0f, B.ca * close_norm.x + B.sa * close_norm.y)));

  float close_dist =
      (B.x - close_pt.x) * close_norm.x + (B.y - close_pt.y) * close_norm.y;

  poly_box_large inside_poly;
  construct_inside_polygon(close_t, &inside_poly);

  float target_t = close_t;
  float target_far = 0.0f;
  float step = B.width / 64.0f;
  for (float h = close_dist, a = cur_course; h >= 0.125f * B.width;) {
    target_t -= step;
    target_far += step * cosf(a);
    h += step * sinf(a);
    a = fmaxf(-(float)M_PI / 4.0f, a - step / B.width);
  }
  v2 target_pt = smooth_point(target_t);
  v2 past_target = smooth_point(target_t - 3.0f * B.width);

  float offset_incr = 0.0625f * B.width;

  float enemy_body_od = ENEMY_BODY_OD0 * B.width;
  float enemy_head_d2 = 64.0f * 64.0f * B.width * B.width;
  bool enemy_head_inside = false;

  int ns = tdarray_length(gdata->data.snakes);
  for (int i = 0; i < ns; i++) {
    snake* sk = gdata->data.snakes + i;
    if (sk->id == B.id || sk->dead) continue;

    float ew = snake_width(sk->sc);
    v2 eh = {sk->xx, sk->yy};
    v2 ea = {eh.x + cosf(sk->ang) * B.width, eh.y + sinf(sk->ang) * B.width};

    if (point_in_poly_large(eh, &inside_poly)) {
      enemy_head_inside = true; /* NTL: relaxes the coil-radius clamp below */
    } else {
      float d1 = dist2(eh.x, eh.y, target_pt.x, target_pt.y);
      float d2_ = dist2(ea.x, ea.y, target_pt.x, target_pt.y);
      float dm = d1 < d2_ ? d1 : d2_;
      if (dm < enemy_head_d2) enemy_head_d2 = dm;
    }

    bool offset_set = false;
    float offset = 0.0f;
    poly_box cpolbody;
    float body_offset = ENEMY_BODY_MULT * (B.width + ew);

    int pn = tdarray_length(sk->pts);
    for (int j = 0; j < pn; j++) {
      body_part* po = sk->pts + j;
      if (po->dying) continue;
      v2 pt = {po->xx, po->yy};
      /* NTL ignores enemy body points that already sit inside our coil. */
      if (point_in_poly_large(pt, &inside_poly)) continue;

      while (!offset_set ||
             (enemy_body_od >= -B.width && point_in_poly(pt, &cpolbody))) {
        if (!offset_set) {
          offset_set = true;
        } else {
          enemy_body_od -= offset_incr;
        }
        offset = body_offset + enemy_body_od;
        body_danger_zone(offset, target_pt, close_norm, close_dist, past_target,
                         close_pt, &cpolbody);
      }
    }
  }

  /* NTL wall handling inside the coil: same 251-point ring as the collision
     check, pushed out of the danger polygon with offset 0.22*(w + od). */
  float wall_od = WALL_OD0 * B.width;
  if (B.near_wall && gdata->data.grd > 0.0f) {
    const float grd = gdata->data.grd;
    bool offset_set = false;
    float offset = 0.0f;
    poly_box cpolbody;

    for (int k = -WALL_HALF_SPAN; k <= WALL_HALF_SPAN; k += WALL_STEP) {
      float wa = B.wall_view_ang + (float)k / grd;
      v2 wp = {grd + cosf(wa) * B.wall_ring_r, grd + sinf(wa) * B.wall_ring_r};

      while (!offset_set ||
             (wall_od >= -B.width && point_in_poly(wp, &cpolbody))) {
        if (!offset_set) {
          offset_set = true;
        } else {
          wall_od -= offset_incr;
        }
        offset = WALL_OFF_MULT * (B.width + wall_od);
        body_danger_zone(offset, target_pt, close_norm, close_dist, past_target,
                         close_pt, &cpolbody);
      }
    }
  }

  float enemy_head_dist = sqrtf(enemy_head_d2);

  /* NTL only adds the +0.25 outward bias when within 1650 of the wall. */
  float target_course = cur_course;
  if (B.wall_dist_grd < COIL_WALL_BIAS_RANGE) target_course += 0.25f;

  float head_prox = -1.0f - (2.0f * target_far - enemy_head_dist) / B.width;
  if (head_prox > 0)
    head_prox = 0.125f * head_prox * head_prox;
  else
    head_prox = -0.500f * head_prox * head_prox;
  target_course = fminf(target_course, head_prox);

  bool body_adj = false;
  float body_adj_course = 0.0f;
  float adj_body = (enemy_body_od - 0.0625f * B.width) / B.width;
  if (adj_body < 0.0f) {
    target_course += adj_body;
    body_adj = true;
    body_adj_course = target_course;
  }

  if (B.near_wall) {
    float adj_wall = (wall_od - 0.0625f * B.width) / B.width;
    target_course = fminf(target_course, target_course + adj_wall);
  }

  /* NTL: tail allowance depends on how much the coil faces the follow target
     (map centre, or a ring point ahead of us when close to the wall). */
  float tail_behind = B.bpts_len - close_t;
  v2 ft = unit_vec((v2){B.follow_tx - B.x, B.follow_ty - B.y});
  float face = ft.x * close_norm.x + ft.y * close_norm.y;
  float allow_tail = B.width * (2.0f - 0.5f * face);
  target_course =
      fminf(target_course,
            (tail_behind - allow_tail + (B.width - close_dist)) / B.width);

  target_course = fminf(
      target_course,
      (-0.5f * (close_dist - (enemy_head_inside ? 1.0f : 4.0f) * B.width)) /
          B.width);

  target_course = fmaxf(target_course, (COIL_MIN_COURSE * close_dist) / B.width);
  target_course = fminf(target_course, 1.0f);

  if (body_adj && target_course > body_adj_course)
    target_course -= COIL_BODY_PENALTY;

  v2 goal_dir = {close_tang.x * cosf(target_course) -
                     o * close_tang.y * sinf(target_course),
                 close_tang.y * cosf(target_course) +
                     o * close_tang.x * sinf(target_course)};
  v2 goal = {B.x + goal_dir.x * 4.0f * B.width,
             B.y + goal_dir.y * 4.0f * B.width};

  if (fabsf(goal.x - B.goal.x) < 1000.0f &&
      fabsf(goal.y - B.goal.y) < 1000.0f) {
    B.goal.x = roundf(goal.x * 0.25f + B.goal.x * 0.75f);
    B.goal.y = roundf(goal.y * 0.25f + B.goal.y * 0.75f);
  } else {
    B.goal = (v2){roundf(goal.x), roundf(goal.y)};
  }
}

static snake* find_me(game_data* gdata) {
  int ns = tdarray_length(gdata->data.snakes);
  for (int i = 0; i < ns; i++)
    if (gdata->data.snakes[i].id == B.id) return gdata->data.snakes + i;
  return NULL;
}

/* NTL fe(): runs every frame once past the circling threshold. Turns toward
   the circle direction by pi / max(8, ~~(min(w,100)/100*25)) and switches to
   coil as soon as the head touches the tail or the first 85% of the body. */
static void to_circle(game_data* gdata) {
  if (!B.enable_follow_circle) return;
  snake* me = find_me(gdata);
  if (!me) return;

  int pn = tdarray_length(me->pts);
  int first = 0;
  while (first < pn && me->pts[first].dying) first++;
  if (first >= pn) return;

  float wsz = fminf(B.width, 100.0f);
  int div = (int)(wsz / 100.0f * TO_CIRCLE_I8);
  if (div < 8) div = 8;

  gdata->bot.output.accel = (bool)B.default_accel;
  B.goal = heading_rel(((float)B.circle_dir * (float)M_PI) / (float)div);

  circ tail = {me->pts[first].xx, me->pts[first].yy,
               (float)(int)(B.head_circle.r / 2.0f)};
  if (circle_intersect(B.head_circle, tail, NULL)) {
    B.stage = 2;
    return;
  }

  float body_r = (float)(int)(B.width / 2.0f);
  circ hc = {B.x, B.y, body_r};
  int limit = (int)ceilf((float)pn * SELF_CHECK_FRAC);
  for (int i = 0; i < limit && i < pn; i++) {
    body_part* po = me->pts + i;
    if (po->dying) continue;
    circ bc = {po->xx, po->yy, body_r};
    if (circle_intersect(hc, bc, NULL)) {
      B.stage = 2;
      return;
    }
  }
}

static void compute_food_goal(game_data* gdata) {
  memset(B.food_angles_set, 0, sizeof(B.food_angles_set));
  int fn = tdarray_length(gdata->data.foods);
  for (int i = 0; i < fn; i++) {
    food* fo = gdata->data.foods + i;
    if (fo->eaten) continue;
    float fd2 = dist2(B.x, B.y, fo->xx, fo->yy);
    circ fc = {fo->xx, fo->yy, 2.0f};
    if (circle_intersect(fc, B.side_l, NULL)) continue;
    if (circle_intersect(fc, B.side_r, NULL)) continue;
    add_food_angle(fo->xx, fo->yy, fd2, fo->sz);
  }
  B.has_food = false;
  float best = -1.0f;
  for (int i = 0; i < MAXARC; i++) {
    if (!B.food_angles_set[i] || B.food_angles[i].sz <= 0.0f) continue;
    if (B.food_angles[i].score > best) {
      best = B.food_angles[i].score;
      B.current_food = B.food_angles[i];
      B.has_food = true;
    }
  }
}

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* NTL Se(): the delayed action. Fires ACTION_MS after the last quiet frame
   (pushed out to COLLISION_MS whenever a collision/encircle decision is made
   while it is pending). */
static void timer_fire(game_data* gdata) {
  if (!B.timer_pending || now_ms() < B.timer_at) return;
  B.timer_pending = false;

  snake* me = find_me(gdata);
  if (!me || me->dead) return;

  if (B.stage == 0) {
    compute_food_goal(gdata);
    B.goal = B.has_food ? (v2){B.current_food.x, B.current_food.y}
                        : (v2){gdata->data.grd, gdata->data.grd};
  } else if (B.stage == 1) {
    to_circle(gdata);
  }
}

static int snake_score(game_data* gdata, snake* sk) {
  if (sk->sct < 0 || sk->fam < 0 || sk->rsc < 0) return 0;
  int sct = sk->sct + sk->rsc;
  return (int)(15.0f * (gdata->data.fpsls[sct] +
                        sk->fam / gdata->data.fmlts[sct] - 1) -
               5);
}

static void every(game_data* gdata) {
  int ns = tdarray_length(gdata->data.snakes);
  if (ns == 0) return;
  snake* me = NULL;
  for (int i = 0; i < ns; i++)
    if (gdata->data.snakes[i].id == B.id) {
      me = gdata->data.snakes + i;
      break;
    }
  if (!me) {
    me = gdata->data.snakes + (ns - 1);
    B.id = me->id;
  }

  B.x = me->xx;
  B.y = me->yy;
  B.ang = me->ang;
  B.ca = cosf(B.ang);
  B.sa = sinf(B.ang);
  B.speed_mult = me->sp / SPEED_BASE;
  B.width = snake_width(me->sc);
  B.radius = B.width / 2.0f;
  B.snake_len = snake_score(gdata, me);

  float spf = fminf(1.0f, B.speed_mult - 1.0f) * B.radius_mult;
  B.head_circle = (circ){B.x + (B.ca * spf / 2.0f) * B.radius,
                         B.y + (B.sa * spf / 2.0f) * B.radius,
                         (B.radius_mult / 2.0f) * B.radius};
  B.side_r = (circ){B.x - (B.y + B.sa * B.width - B.y),
                    B.y + (B.x + B.ca * B.width - B.x), B.width * B.speed_mult};
  B.side_l = (circ){B.x + (B.y + B.sa * B.width - B.y),
                    B.y - (B.x + B.ca * B.width - B.x), B.width * B.speed_mult};

  /* ---- NTL wall bookkeeping ---- */
  const float grd = gdata->data.grd;
  float dcx = B.x - grd, dcy = B.y - grd;
  float dctr = sqrtf(dcx * dcx + dcy * dcy);
  float safe = safe_border_radius(gdata);
  bool shrinking = grd > 0.0f && safe > 0.0f && safe < grd - 1.0f;
  float edge = shrinking ? safe : grd;
  B.wall_dist = fabsf(edge - dctr);
  B.wall_dist_grd = fabsf(grd - dctr);
  B.near_wall = grd > 0.0f && B.wall_dist < WALL_RANGE;
  B.wall_ring_r = shrinking ? safe : WALL_RING_SCALE * grd;
  B.wall_view_ang = atan2f(B.y - grd, B.x - grd);
  if (grd > 0.0f && B.wall_dist_grd < WALL_RANGE) {
    float fa = B.wall_view_ang + 64.0f * (float)B.circle_dir / grd;
    B.follow_tx = grd + cosf(fa) * WALL_RING_SCALE * grd;
    B.follow_ty = grd + sinf(fa) * WALL_RING_SCALE * grd;
  } else {
    B.follow_tx = grd;
    B.follow_ty = grd;
  }
}

#if SBOT_BORDER_EMERGENCY
/* Border safety must outrank every behavioural stage. The original port ran
   check_collision() only outside self-circle stage 2, so a coiling snake could
   keep following its own body while touching the wall. This predictive guard
   breaks food/coil state early and turns toward a safe inward point. */
static bool avoid_border_emergency(game_data* gdata) {
  const float border_radius = safe_border_radius(gdata);
  const float center = gdata->data.grd;
  if (border_radius <= 0.0f || center <= 0.0f) return false;

  float dx = B.x - center;
  float dy = B.y - center;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 1.0f) return false;

  float outward_x = dx / dist;
  float outward_y = dy / dist;
  float clearance = border_radius - dist - B.radius;
  float guard = fmaxf(700.0f,
                      B.head_circle.r + B.width * (4.0f + B.speed_mult));
  float lookahead = fmaxf(500.0f,
                          B.width * (5.0f + 2.0f * B.speed_mult));
  float predicted_x = B.x + B.ca * lookahead;
  float predicted_y = B.y + B.sa * lookahead;
  float predicted_dist =
      sqrtf(dist2(predicted_x, predicted_y, center, center));
  float predicted_clearance = border_radius - predicted_dist - B.radius;
  float outward_heading = B.ca * outward_x + B.sa * outward_y;

  bool imminent = clearance <= guard * 0.45f ||
                  predicted_clearance <= guard * 0.70f;
  bool unsafe_course = clearance <= guard &&
                       (outward_heading > -0.25f || B.stage == 2);
  if (!imminent && !unsafe_course) return false;

  float retreat = fmaxf(1000.0f, guard * 1.75f);
  B.goal = (v2){roundf(B.x - outward_x * retreat),
                roundf(B.y - outward_y * retreat)};
  B.stage = 0;
  B.has_food = false;
  B.timer_pending = false;
  gdata->bot.output.accel = false;

  g_bot_dbg.avoid_active = true;
  g_bot_dbg.avoid_ix = center + outward_x * border_radius;
  g_bot_dbg.avoid_iy = center + outward_y * border_radius;
  g_bot_dbg.avoid_fwd_x = predicted_x;
  g_bot_dbg.avoid_fwd_y = predicted_y;
  return true;
}

#endif

void sbot_init(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  memset(&B, 0, sizeof(B));
  B.stage = 0;
  B.timer_pending = false;
  B.default_accel = 0;
  B.circle_dir = +1;
  B.enable_encircle = true;
  B.enable_follow_circle = true;
  gdata->bot.output.accel = false;
  gdata->bot.output.xm = 0;
  gdata->bot.output.ym = 0;
}

void sbot_go(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;
  sbot* bot = &gdata->bot;
  B.radius_mult = (float)NTL_RADIUS_MULT;
  (void)usrs->bot_radius_mult; /* NTL parity: fixed multiplier */
  B.follow_circle_length = usrs->bot_follow_circle_score;

  g_bot_dbg.avoid_active   = false;
  g_bot_dbg.encircle_state = 0;

  if (tdarray_length(gdata->data.snakes) == 0) return;

  every(gdata);
  timer_fire(gdata);

  /* NTL go(): below the threshold we grow; above it we run the to-circle
     check every frame until the head touches the body, then coil. */
  if (B.enable_follow_circle) {
    if (B.snake_len < B.follow_circle_length)
      B.stage = 0;
    else if (B.stage != 2)
      to_circle(gdata);
  } else {
    B.stage = 2;
  }
  if (B.has_food && B.stage != 0) B.has_food = false;

#if SBOT_BORDER_EMERGENCY
  if (avoid_border_emergency(gdata)) {
    /* emergency goal already selected */
  } else
#endif
  if (B.stage == 2) {
    bot->output.accel = (bool)B.default_accel;
    follow_circle_self(gdata);
  } else if (check_collision(gdata) || check_encircle(gdata)) {
    if (B.timer_pending) B.timer_at = now_ms() + COLLISION_MS;
  } else {
    if (B.snake_len > B.follow_circle_length) B.stage = 1;
    if (!B.timer_pending) {
      B.timer_pending = true;
      B.timer_at = now_ms() + ACTION_MS;
    }
    bot->output.accel = (bool)B.default_accel;
  }

  bot->output.xm = (B.goal.x - gdata->data.view_xx) * gdata->data.gsc;
  bot->output.ym = (B.goal.y - gdata->data.view_yy) * gdata->data.gsc;

  g_bot_dbg.active        = true;
  g_bot_dbg.bx            = B.x;
  g_bot_dbg.by            = B.y;
  g_bot_dbg.brad          = B.radius;
  g_bot_dbg.head_cx       = B.head_circle.x;
  g_bot_dbg.head_cy       = B.head_circle.y;
  g_bot_dbg.head_cr       = B.head_circle.r;
  g_bot_dbg.has_food      = B.has_food;
  g_bot_dbg.food_x        = B.current_food.x;
  g_bot_dbg.food_y        = B.current_food.y;
  g_bot_dbg.goal_x        = B.goal.x;
  g_bot_dbg.goal_y        = B.goal.y;
  g_bot_dbg.stage         = B.stage;
  g_bot_dbg.accel         = bot->output.accel;
  g_bot_dbg.encircle_r1   = B.radius * (float)B.radius_mult;
  g_bot_dbg.encircle_r2   = B.radius * ENCIRCLE_DIST_MULT;
  for (int _i = 0; _i < 16; _i++) {
    g_bot_dbg.arc_coll[_i] = B.coll_angles_set[_i];
    g_bot_dbg.arc_food[_i] = B.food_angles_set[_i];
  }
  g_bot_dbg.coll_n = 0;
  for (int _i = 0; _i < B.coll_pts_n && _i < SBOT_DBG_MAX_COLL; _i++) {
    g_bot_dbg.coll_x[_i]    = B.coll_pts[_i].x;
    g_bot_dbg.coll_y[_i]    = B.coll_pts[_i].y;
    g_bot_dbg.coll_r[_i]    = B.coll_pts[_i].r;
    g_bot_dbg.coll_type[_i] = B.coll_pts[_i].type;
    g_bot_dbg.coll_d2[_i]   = B.coll_pts[_i].d2;
    g_bot_dbg.coll_n++;
  }
}

void sbot_destroy(tenv* env) { (void)env; }
