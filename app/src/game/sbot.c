#include "sbot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

sbot_dbg g_bot_dbg;

#include "../user.h"

#define ARC_SIZE (M_PI / 8.0f)
#define MAXARC 16
#define SPEED_BASE 5.78f
#define FRONT_ANGLE (M_PI / 2.0f)
#define ENCIRCLE_THRESH 0.5f
#define ENCIRCLE_ALL_THRESH 0.5f
#define ENCIRCLE_DIST_MULT 30.0f
#define NTL_RADIUS_MULT 12
#define SNAKE_WIDTH_MULT 29.2f
#define NEARBY_BOX 2400.0f

#define WALL_RANGE 3500.0f
#define WALL_RING_SCALE 0.98f
#define WALL_PT_RADIUS 8.0f
#define WALL_HALF_SPAN 1000
#define WALL_STEP 8
#define WALL_GROUP 511

#define COIL_WALL_BIAS_RANGE 1650.0f
#define ENEMY_BODY_MULT 0.38f
#define ENEMY_BODY_OD0 0.15f
#define WALL_OFF_MULT 0.22f
#define WALL_OD0 0.25f
#define COIL_MIN_COURSE (-0.8f)
#define COIL_BODY_PENALTY 0.16f
#ifndef SBOT_FORAGE
#define SBOT_FORAGE 0
#endif
#ifndef SBOT_HUG
#define SBOT_HUG 0
#endif
#define HUG_BODY_MULT 0.10f
#define HUG_BODY_OD0 0.06f
#define TRAPPED_HOLD_MS 1500.0
#define TO_CIRCLE_I8 25.0f
#define SELF_CHECK_FRAC 0.85f

#define ACTION_MS 200.0
#define COLLISION_MS 1000.0

#ifndef SBOT_BORDER_EMERGENCY
#define SBOT_BORDER_EMERGENCY 1
#endif

#define MAX_BODY_PTS 8192
#define MAX_HULL_PTS 32
#define MAX_INSIDE_PTS 4096
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
  float x, y, fx, fy, ang, aang, ca, sa;
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

  bool near_wall;
  float wall_dist;
  float wall_ring_r;
  float wall_view_ang;
  float wall_dist_grd;
  float follow_tx, follow_ty;

  double forage_until;
  double forage_cool;
  double forage_check_at;
  double trapped_until;
} bot_state;

static bot_state B;

#define SEC_N 48
#define SEC_STEP ((float)PI2 / (float)SEC_N)
#define SAFE_RANGE 1600.0f
#define THREAT_RANGE 2200.0f
#define S_OBST_MAX 400
#define S_THR_MAX 12
#define ENC_BINS 24
#define ENC_SOLO_BINS 14
#define ENC_ALL_BINS 20

typedef struct {
  float x, y, r;
  int si;
  bool ghost;
} s_obst;
typedef struct {
  float x, y, ang, dng;
  int si;
} s_thr;

static struct {
  s_obst obs[S_OBST_MAX];
  int nobs;
  s_thr thr[S_THR_MAX];
  int nthr;
  float nearest_head;
  float nearest_obs;
  float max_dng;
  unsigned enc_mask, enc_all_mask;
  int enc_bits, enc_all_bits;
  bool inside_used;
  float enc_range, rturn, L, hard, need;
  float block[SEC_N];
  float head[SEC_N];
} S;

static poly_box_large g_inside;
static bool g_inside_valid;

static inline float dist2(float ax, float ay, float bx, float by) {
  float dx = ax - bx, dy = ay - by;
  return dx * dx + dy * dy;
}

static float ntl_atan2(float yf, float xf) {
  double y = yf, x = xf;
  double c = M_PI / 4.0;
  double g = fabs(y) + 1e-10;
  double r;
  if (x < 0) {
    r = (x + g) / (g - x);
    c = 3.0 * M_PI / 4.0;
  } else {
    r = (x - g) / (x + g);
  }
  c += (0.1963 * r * r - 0.9817) * r;
  return (float)(y < 0 ? -c : c);
}

static inline float safe_border_radius(const game_data* gdata) {
  float smoothed = gdata->data.flux_grd;
  float server = gdata->data.real_flux_grd;
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
  c1 = (circ){roundf(c1.x), roundf(c1.y), roundf(c1.r)};
  c2 = (circ){roundf(c2.x), roundf(c2.y), roundf(c2.r)};
  float br = c1.r + c2.r;
  if (c1.x + br <= c2.x || c2.x + br <= c1.x) return false;
  if (c1.y + br <= c2.y || c2.y + br <= c1.y) return false;
  if (dist2(c1.x, c1.y, c2.x, c2.y) >= br * br) return false;
  if (out) {
    out->x = (c1.x * c2.r + c2.x * c1.r) / br;
    out->y = (c1.y * c2.r + c2.y * c1.r) / br;
    out->a = ntl_atan2(out->y - B.y, out->x - B.x);
  }
  return true;
}

static v2 heading_abs(float angle) {
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
  float ang = ntl_atan2(roundf(sp->y - B.y), roundf(sp->x - B.x));
  int ai = ang_index(ang);
  float adj = sqrtf(sp->d2) - sp->r;
  float ad2 = roundf(adj * adj);
  if (!B.coll_angles_set[ai] || B.coll_angles[ai].d2 > sp->d2) {
    B.coll_angles[ai] =
        (coll_ang){roundf(sp->x), roundf(sp->y), ang, ad2, sp->r, sp->si, ai};
    B.coll_angles_set[ai] = true;
  }
}

static void add_food_angle(float fx, float fy, float fd2, float fsz) {
  float ang = ntl_atan2(roundf(fy - B.y), roundf(fx - B.x));
  int ai = ang_index(ang);
  if (B.coll_angles_set[ai]) {
    float cd = sqrtf(B.coll_angles[ai].d2);
    float fd = sqrtf(fd2);
    if (cd <= fd + (B.radius * B.radius_mult * B.speed_mult) / 2.0f) return;
  }
  float score = fsz * fsz / fd2;
  if (!B.food_angles_set[ai]) {
    B.food_angles[ai] =
        (food_ang){roundf(fx), roundf(fy), ang,  fabsf(ang_between(ang, B.aang)),
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

    float hx = s->xx + cosf(s->ehang) * srm / 2.0f;
    float hy = s->yy + sinf(s->ehang) * srm / 2.0f;
    float hd2 = dist2(B.x, B.y, hx, hy);

    coll_pt head = {hx, hy, hd2, B.head_circle.r, i, 0};
    add_coll_angle(&head);
    if (B.coll_pts_n < MAX_COLL_PTS) B.coll_pts[B.coll_pts_n++] = head;

    int pn = tdarray_length(s->pts);
    for (int j = 0; j < pn; j++) {
      body_part* po = s->pts + j;
      if (po->dying) continue;
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
  float ang = ntl_atan2(roundf(py - B.y), roundf(px - B.x));
  return fabsf(ang_between(ang, B.aang)) < FRONT_ANGLE;
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
  int high = 0, en_all = 0;
  const float enc_r2 = S.enc_range * S.enc_range;
  for (int i = 0; i < MAXARC; i++) {
    if (!B.coll_angles_set[i]) continue;
    coll_ang* ca = &B.coll_angles[i];
    if (ca->si >= 0 && ca->si < 512) {
      if (ca->d2 <= enc_r2 && ++en[ca->si] > high) high = en[ca->si];
    }
    float ed = B.radius * ENCIRCLE_DIST_MULT;
    if (ca->d2 < ed * ed) en_all++;
  }
  if (high > (int)(MAXARC * ENCIRCLE_THRESH)) {
    heading_best_angle();
    gdata->bot.output.accel = true;
    g_bot_dbg.encircle_state = 1;
    return true;
  }
  if (en_all > (int)(MAXARC * ENCIRCLE_ALL_THRESH)) {
    heading_best_angle();
    gdata->bot.output.accel = true;
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

  float hx0 = B.x + B.fx, hy0 = B.y + B.fy;
  B.bpts[B.bpts_n++] = (body_pt){hx0, hy0, 0.0f};
  float lx = hx0, ly = hy0, l = 0.0f;
  int pn = tdarray_length(me->pts);

  for (int p = pn - 1; p >= 0 && B.bpts_n < MAX_BODY_PTS; p--) {
    body_part* po = me->pts + p;
    if (po->dying) continue;
    float gx = po->xx + po->fx, gy = po->yy + po->fy;
    l += sqrtf(dist2(lx, ly, gx, gy));
    B.bpts[B.bpts_n++] = (body_pt){gx, gy, l};
    lx = gx;
    ly = gy;
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
    start_d2 = dist2(B.bpts[0].x, B.bpts[0].y, B.bpts[start_n].x, B.bpts[start_n].y);
    if (start_d2 < prev || start_n == B.bpts_n - 1) break;
  }
  if (start_n >= B.bpts_n || start_n <= 1) return B.bpts_len;

  int min_n = start_n;
  float min_d2 = start_d2;
  for (int n = min_n + 1; n < B.bpts_n; n++) {
    float d2 = dist2(B.bpts[0].x, B.bpts[0].y, B.bpts[n].x, B.bpts[n].y);
    if (d2 < min_d2) {
      min_n = n;
      min_d2 = d2;
    }
  }

  int next_n;
  float next_d2;
  if (min_n == B.bpts_n - 1) {
    next_n = min_n - 1;
    next_d2 = dist2(B.bpts[0].x, B.bpts[0].y, B.bpts[next_n].x, B.bpts[next_n].y);
  } else {
    float dm = dist2(B.bpts[0].x, B.bpts[0].y, B.bpts[min_n - 1].x, B.bpts[min_n - 1].y);
    float dp = dist2(B.bpts[0].x, B.bpts[0].y, B.bpts[min_n + 1].x, B.bpts[min_n + 1].y);
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
  float dx = B.x + B.fx - cx, dy = B.y + B.fy - cy;
  B.circle_dir = (-dy * B.ca + dx * B.sa > 0.0f) ? -1 : +1;
}

static void body_danger_zone(float offset, v2 target_pt,
                             v2 target_norm,
                             float close_dist,
                             v2 past_target, v2 close_pt, poly_box* out) {
  int o = B.circle_dir;
  const float hx = hx + B.fx, hy = hy + B.fy;
  v2 raw[10];
  raw[0] = (v2){hx - o * offset * B.sa, hy + o * offset * B.ca};
  raw[1] = (v2){hx + B.width * B.ca + offset * (B.ca - o * B.sa),
                hy + B.width * B.sa + offset * (B.sa + o * B.ca)};
  raw[2] = (v2){hx + 1.75f * B.width * B.ca + o * 0.3f * B.width * B.sa +
                    offset * (B.ca - o * B.sa),
                hy + 1.75f * B.width * B.sa - o * 0.3f * B.width * B.ca +
                    offset * (B.sa + o * B.ca)};
  raw[3] = (v2){hx + 2.5f * B.width * B.ca + o * 0.7f * B.width * B.sa +
                    offset * (B.ca - o * B.sa),
                hy + 2.5f * B.width * B.sa - o * 0.7f * B.width * B.ca +
                    offset * (B.sa + o * B.ca)};
  raw[4] = (v2){
      hx + 3.0f * B.width * B.ca + o * 1.2f * B.width * B.sa + offset * B.ca,
      hy + 3.0f * B.width * B.sa - o * 1.2f * B.width * B.ca + offset * B.sa};
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

static double now_ms(void);

static inline float travel_ms(float sp, float ms) { return sp * ms / 32.0f; }

static inline int popcount24(unsigned v) {
  int c = 0;
  while (v) {
    v &= v - 1;
    c++;
  }
  return c;
}

static inline int sec_of(float a) {
  a = fmodf(a, (float)PI2);
  if (a < 0) a += (float)PI2;
  int i = (int)(a / SEC_STEP);
  return i >= SEC_N ? SEC_N - 1 : i;
}

static inline float adiff(float a, float b) {
  float d = fmodf(fabsf(a - b), (float)PI2);
  return d > (float)M_PI ? (float)PI2 - d : d;
}

static inline float adiff_s(float a, float b) {
  float d = fmodf(a - b, (float)PI2);
  if (d < -(float)M_PI) d += (float)PI2;
  if (d > (float)M_PI) d -= (float)PI2;
  return d;
}

static void safety_gather(game_data* gdata) {
  S.nobs = 0;
  S.nthr = 0;
  S.nearest_head = S.nearest_obs = 1e18f;
  S.max_dng = 0.0f;
  S.enc_mask = S.enc_all_mask = 0u;
  S.enc_bits = S.enc_all_bits = 0;
  S.inside_used = g_inside_valid;

  const float my_r = B.radius;
  const float sp = B.speed_mult * SPEED_BASE;
  const float ratio = fminf(2.6f, fmaxf(1.0f, B.speed_mult));
  S.rturn = (my_r * 8.0f + 120.0f) * ratio;
  S.L = fminf(SAFE_RANGE, fmaxf(700.0f, S.rturn * 2.2f + 320.0f + sp * 26.0f));
  S.hard = my_r * 0.6f + 58.0f + sp * 8.0f;
  S.need = S.rturn * 0.8f + my_r * 0.7f + 60.0f;
  S.enc_range = fminf(1500.0f, fmaxf(600.0f, B.width * 14.0f + 300.0f));
  const float R2 = SAFE_RANGE * SAFE_RANGE;
  const float enc_r2 = S.enc_range * S.enc_range;

  int ns = tdarray_length(gdata->data.snakes);
  static int order[512];
  static float od2[512];
  int no = 0;
  for (int i = 0; i < ns && no < 512; i++) {
    snake* o = gdata->data.snakes + i;
    if (o->id == B.id || o->dead) continue;
    float dx = o->xx - B.x, dy = o->yy - B.y;
    float d2 = dx * dx + dy * dy;
    int k = no++;
    while (k > 0 && od2[k - 1] > d2) {
      order[k] = order[k - 1];
      od2[k] = od2[k - 1];
      k--;
    }
    order[k] = i;
    od2[k] = d2;
  }
  const int stride = ns > 24 ? 4 : (ns > 12 ? 3 : 2);

  for (int oi = 0; oi < no; oi++) {
    int si = order[oi];
    snake* o = gdata->data.snakes + si;
    const float er = snake_width(o->sc) / 2.0f;
    const float ca = cosf(o->ang), sa = sinf(o->ang);
    const bool boosting = o->sp > 7.0f;

    float tx = B.x - o->xx, ty = B.y - o->yy;
    float d = sqrtf(tx * tx + ty * ty);
    float align = d > 1.0f ? (ca * tx + sa * ty) / d : 0.0f;
    float dng = (align > 0 ? align : 0) * (1.0f - d / THREAT_RANGE);
    if (d > THREAT_RANGE) dng = 0.0f;
    if (boosting) dng *= 1.4f;
    if (dng > 1.0f) dng = 1.0f;

    const float thr = boosting ? 0.80f : 0.87f;
    float over = (dng - thr * 0.6f) / (1.0f - thr * 0.6f);
    over = fminf(1.0f, fmaxf(0.0f, over));

    const bool head_inside =
        g_inside_valid &&
        point_in_poly_large((v2){o->xx + o->fx, o->yy + o->fy}, &g_inside);
    if (head_inside) {
      dng = 0.0f;
      over = 0.0f;
    }

    float hd0 = d - er;
    if (!head_inside) {
      if (hd0 < S.nearest_head) S.nearest_head = hd0;
      if (hd0 < S.nearest_obs) S.nearest_obs = hd0;
      if (dng > S.max_dng) S.max_dng = dng;
    }

    float psp = o->sp;
    if (align > 0.6f && d < 900.0f && psp < SPEED_BASE * 1.8f)
      psp = SPEED_BASE * 1.8f;
    const float headr = er * (1.1f + 1.9f * over) + 14.0f;
    const float times[3] = {0.0f, 300.0f, boosting ? 900.0f : 650.0f};
    for (int q = 0; q < 3 && !head_inside; q++) {
      float pd = er * 0.8f + travel_ms(psp, times[q]);
      float hx = o->xx + o->fx + ca * pd, hy = o->yy + o->fy + sa * pd;
      float hdx = hx - B.x, hdy = hy - B.y;
      if (hdx * hdx + hdy * hdy >= R2) continue;
      if (S.nobs < S_OBST_MAX) S.obs[S.nobs++] = (s_obst){hx, hy, headr, si, true};
    }

    if (!head_inside && dng > 0.18f && d < THREAT_RANGE && S.nthr < S_THR_MAX)
      S.thr[S.nthr++] = (s_thr){o->xx, o->yy, o->ang, dng, si};

    if (!o->pts) continue;
    int pcount = tdarray_length(o->pts);
    const bool trapped_now = SBOT_HUG && B.trapped_until > now_ms();
    const float hug = (!trapped_now && d < 0.10f * THREAT_RANGE) ? 26.0f : 8.0f;
    const float bpad = er + hug + stride * 1.5f;
    unsigned emask = 0;
    for (int p = 0; p < pcount && S.nobs < S_OBST_MAX; p += stride) {
      body_part* bp = o->pts + p;
      if (bp->dying) continue;
      float bx = bp->xx + bp->fx, by = bp->yy + bp->fy;
      float dx = bx - B.x, dy = by - B.y;
      float d2 = dx * dx + dy * dy;
      if (d2 > R2) continue;
      if (g_inside_valid && point_in_poly_large((v2){bx, by}, &g_inside))
        continue;
      if (d2 < enc_r2) {
        float ba = atan2f(dy, dx);
        if (ba < 0) ba += (float)PI2;
        int bin = (int)(ba * ((float)ENC_BINS / (float)PI2));
        if (bin >= 0 && bin < ENC_BINS) emask |= 1u << bin;
      }
      float dd = sqrtf(d2) - er;
      if (dd < S.nearest_obs) S.nearest_obs = dd;
      S.obs[S.nobs++] = (s_obst){bx, by, bpad, si, false};
    }
    int ebits = popcount24(emask);
    if (ebits > S.enc_bits) {
      S.enc_bits = ebits;
      S.enc_mask = emask;
    }
    S.enc_all_mask |= emask;
  }
  S.enc_all_bits = popcount24(S.enc_all_mask);
}

static void field_build(game_data* gdata) {
  const float L = S.L, my_r = B.radius;
  for (int i = 0; i < SEC_N; i++) {
    S.block[i] = L;
    S.head[i] = 0.0f;
  }

  for (int gi = 0; gi < S.nthr; gi++) {
    float dng = S.thr[gi].dng;
    if (dng < 0.12f) continue;
    float ca = cosf(S.thr[gi].ang), sa = sinf(S.thr[gi].ang);
    for (int st = 0; st < 3; st++) {
      float proj = st * 140.0f;
      float qx = S.thr[gi].x + ca * proj - B.x;
      float qy = S.thr[gi].y + sa * proj - B.y;
      float d = sqrtf(qx * qx + qy * qy);
      if (d < 1.0f || d > 1700.0f) continue;
      float w = dng * (1.0f - d / 1700.0f) * (st == 0 ? 1.0f : 0.55f);
      int c = sec_of(atan2f(qy, qx));
      for (int o = -2; o <= 2; o++) {
        int si = ((c + o) % SEC_N + SEC_N) % SEC_N;
        S.head[si] += w * (1.0f - 0.22f * (float)(o < 0 ? -o : o));
      }
    }
  }

  for (int i = 0; i < S.nobs; i++) {
    float qx = S.obs[i].x - B.x, qy = S.obs[i].y - B.y;
    float re = S.obs[i].r + my_r * 0.55f + 10.0f;
    float d2 = qx * qx + qy * qy;
    if (d2 >= (L + re) * (L + re)) continue;
    float d = sqrtf(d2);
    float surf = d - re;
    if (surf < 0) surf = 0;
    if (surf >= L) continue;
    float ai = atan2f(qy, qx);
    float sr = d > 1.0f ? re / d : 1.0f;
    if (sr > 1) sr = 1;
    float hw = asinf(sr);
    if (hw > 1.35f) hw = 1.35f;
    int i0 = (int)floorf((ai - hw) / SEC_STEP);
    int i1 = (int)ceilf((ai + hw) / SEC_STEP);
    for (int k = i0; k <= i1; k++) {
      int si = ((k % SEC_N) + SEC_N) % SEC_N;
      if (surf < S.block[si]) S.block[si] = surf;
    }
  }

  const float c = gdata->data.grd;
  if (c > 100.0f) {
    float Reff = B.wall_ring_r - my_r - 90.0f;
    float qx = B.x - c, qy = B.y - c;
    float q2 = qx * qx + qy * qy;
    float dc = sqrtf(q2);
    if (dc > Reff - L) {
      for (int i = 0; i < SEC_N; i++) {
        float a = ((float)i + 0.5f) * SEC_STEP;
        float ux = cosf(a), uy = sinf(a);
        float b = qx * ux + qy * uy;
        float disc = b * b + Reff * Reff - q2;
        float t = -b + sqrtf(disc > 0 ? disc : 0);
        if (t < 0) t = 0;
        if (t < S.block[i]) S.block[i] = t;
      }
    }
  }
}

static bool arc_clear(float target_ang) {
  const float my_r = B.radius;
  float ang = B.ang, x = B.x, y = B.y;
  const float step = my_r * 1.6f + 45.0f;
  const float dth = step / (S.rturn > 1.0f ? S.rturn : 1.0f);
  for (int n = 0; n < 8; n++) {
    float diff = adiff_s(target_ang, ang);
    if (fabsf(diff) <= dth)
      ang = target_ang;
    else
      ang += diff > 0 ? dth : -dth;
    x += cosf(ang) * step;
    y += sinf(ang) * step;
    for (int i = 0; i < S.nobs; i++) {
      float qx = S.obs[i].x - x, qy = S.obs[i].y - y;
      float re = S.obs[i].r + my_r * 0.55f + 8.0f;
      if (qx * qx + qy * qy < re * re) return false;
    }
    if (fabsf(adiff_s(target_ang, ang)) < 0.01f) break;
  }
  return true;
}

static float safety_pick(float goal_ang, float goal_w) {
  static float sc[SEC_N];
  float best = -1e30f;
  int bi = 0;
  for (int i = 0; i < SEC_N; i++) {
    float a = ((float)i + 0.5f) * SEC_STEP;
    float clear = S.block[i];
    float turn = adiff(a, B.ang);
    float v;
    if (clear < S.hard) {
      v = -1e6f + clear * 2.0f - turn * 26.0f;
    } else {
      float passable = fminf(1.0f, clear / S.need);
      float safe = clear / S.L;
      if (clear < S.need) safe *= 0.45f;
      if (S.head[i] < 0.15f && safe < 0.88f) safe = 0.88f;
      v = 2.8f * safe;
      if (clear > S.L * 0.85f) v += 0.7f;
      float bodypass = passable;
      if (S.head[i] < 0.12f) {
        float bp2 = fminf(1.0f, clear / (B.radius * 1.5f + 90.0f));
        if (bp2 > bodypass) bodypass = bp2;
      }
      v += goal_w * 0.5f * (1.0f + cosf(adiff(a, goal_ang))) * bodypass * bodypass;
      v -= 2.4f * fminf(S.head[i], 1.5f);
      v += 0.45f * 0.5f * (1.0f + cosf(turn));
      if (turn > 2.1f) v -= 1.6f;
      if (turn > 0.45f && !arc_clear(a)) v -= 4.0f;
    }
    sc[i] = v;
    if (v > best) {
      best = v;
      bi = i;
    }
  }
  float sm = sc[(bi - 1 + SEC_N) % SEC_N], sp1 = sc[(bi + 1) % SEC_N], s0 = sc[bi];
  float delta = 0.0f;
  if (s0 > -1e5f && sm > -1e5f && sp1 > -1e5f) {
    float den = sm - 2.0f * s0 + sp1;
    if (den < -1e-4f) {
      delta = 0.5f * (sm - sp1) / den;
      delta = fminf(0.5f, fmaxf(-0.5f, delta));
    }
  }
  return ((float)bi + 0.5f + delta) * SEC_STEP;
}

static bool safety_steer(void) {
  float gdx = B.goal.x - B.x, gdy = B.goal.y - B.y;
  if (gdx * gdx + gdy * gdy < 4.0f) return false;
  float gang = atan2f(gdy, gdx);
  int gi = sec_of(gang);
  int gl = (gi + SEC_N - 1) % SEC_N, gr = (gi + 1) % SEC_N;
  float clear = fminf(S.block[gi], fminf(S.block[gl], S.block[gr]));
  float hd = fmaxf(S.head[gi], fmaxf(S.head[gl], S.head[gr]));
  float hard = S.hard * (B.stage == 2 ? 0.4f : 1.0f);
  if (clear >= hard && hd <= 0.5f) return false;
  float best = safety_pick(gang, 1.1f);
  B.goal = (v2){roundf(B.x + cosf(best) * 1200.0f),
                roundf(B.y + sinf(best) * 1200.0f)};
  g_bot_dbg.avoid_active = true;
  g_bot_dbg.avoid_ix = B.goal.x;
  g_bot_dbg.avoid_iy = B.goal.y;
  g_bot_dbg.avoid_fwd_x = B.x + B.ca * 600.0f;
  g_bot_dbg.avoid_fwd_y = B.y + B.sa * 600.0f;
  return (hd > 0.5f || S.max_dng > 0.6f) &&
         S.nearest_head < B.radius + 520.0f &&
         S.block[sec_of(best)] > S.need;
}

static bool boost_lane_ok(game_data* gdata) {
  float ang = atan2f(B.goal.y - B.y, B.goal.x - B.x);
  const float my_r = B.radius;
  const float dist = travel_ms(14.0f, 620.0f) + my_r * 1.2f;
  const float dx = cosf(ang), dy = sinf(ang);
  const float pad = my_r * 0.55f + 24.0f;
  for (int i = 0; i < S.nobs; i++) {
    float ox = S.obs[i].x - B.x, oy = S.obs[i].y - B.y;
    float fwd = ox * dx + oy * dy;
    if (S.obs[i].ghost ? fwd < 0.0f : fwd < -S.obs[i].r) continue;
    if (fwd > dist + S.obs[i].r) continue;
    float lat = fabsf(-ox * dy + oy * dx);
    if (lat < S.obs[i].r + pad) return false;
  }
  const float c = gdata->data.grd;
  if (c > 100.0f) {
    float br = B.wall_ring_r - my_r - 120.0f;
    float ex = B.x + dx * dist - c, ey = B.y + dy * dist - c;
    if (ex * ex + ey * ey > br * br) return false;
  }
  return true;
}

#define FORAGE_RANGE 700.0f
#define FORAGE_MIN_SZ 12.0f
#define FORAGE_MAX_DNG 0.30f
#define FORAGE_MIN_HEAD 1100.0f
#define FORAGE_MIN_WALL 1200.0f
#define FORAGE_MS 5000.0
#define FORAGE_COOL_MS 2500.0
#define FORAGE_CHECK_MS 250.0
static bool forage_pick(game_data* gdata, float* gx, float* gy) {
  if (S.max_dng >= FORAGE_MAX_DNG || S.nearest_head < FORAGE_MIN_HEAD)
    return false;
  if (B.near_wall && B.wall_dist < FORAGE_MIN_WALL) return false;
  float sum[MAXARC], near2[MAXARC], fx[MAXARC], fy[MAXARC];
  for (int i = 0; i < MAXARC; i++) {
    sum[i] = 0.0f;
    near2[i] = 1e18f;
  }
  const float R2 = FORAGE_RANGE * FORAGE_RANGE;
  int fn = tdarray_length(gdata->data.foods);
  for (int i = 0; i < fn; i++) {
    food* fo = gdata->data.foods + i;
    if (fo->eaten) continue;
    float dx = fo->xx - B.x, dy = fo->yy - B.y;
    float d2 = dx * dx + dy * dy;
    if (d2 > R2 || d2 < 1.0f) continue;
    int sc = ang_index(ntl_atan2(roundf(dy), roundf(dx)));
    sum[sc] += fo->sz;
    if (d2 < near2[sc]) {
      near2[sc] = d2;
      fx[sc] = fo->xx;
      fy[sc] = fo->yy;
    }
  }
  int best = -1;
  float bv = 0.0f;
  for (int i = 0; i < MAXARC; i++) {
    if (sum[i] < FORAGE_MIN_SZ) continue;
    float dist = sqrtf(near2[i]);
    float ang = atan2f(fy[i] - B.y, fx[i] - B.x);
    int gi = sec_of(ang);
    float clear = fminf(S.block[gi], fminf(S.block[(gi + 1) % SEC_N],
                                           S.block[(gi + SEC_N - 1) % SEC_N]));
    if (clear < dist + B.radius * 2.0f + 60.0f) continue;
    float v = sum[i] / (1.0f + dist / 300.0f);
    if (v > bv) {
      bv = v;
      best = i;
    }
  }
  if (best < 0) return false;
  *gx = fx[best];
  *gy = fy[best];
  return true;
}

static void coil_note_wrapped(void) {
  if (!S.inside_used) return;
  bool solo = S.enc_bits >= ENC_SOLO_BINS;
  bool multi = S.enc_all_bits >= ENC_ALL_BINS &&
               S.nearest_obs < B.radius + 300.0f;
  if (solo || multi) B.trapped_until = now_ms() + TRAPPED_HOLD_MS;
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

  const float ax = B.x + B.fx, ay = B.y + B.fy;
  float close_dist =
      (ax - close_pt.x) * close_norm.x + (ay - close_pt.y) * close_norm.y;

  poly_box_large* ip = &g_inside;
  construct_inside_polygon(close_t, ip);
  g_inside_valid = true;

  coil_note_wrapped();
  const bool hug = SBOT_HUG && B.trapped_until > now_ms();

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

  float enemy_body_od = (hug ? HUG_BODY_OD0 : ENEMY_BODY_OD0) * B.width;
  float enemy_head_d2 = 64.0f * 64.0f * B.width * B.width;
  bool enemy_head_inside = false;

  int ns = tdarray_length(gdata->data.snakes);
  for (int i = 0; i < ns; i++) {
    snake* sk = gdata->data.snakes + i;
    if (sk->id == B.id || sk->dead) continue;

    float ew = snake_width(sk->sc);
    v2 eh = {sk->xx + sk->fx, sk->yy + sk->fy};
    v2 ea = {eh.x + cosf(sk->ang) * B.width, eh.y + sinf(sk->ang) * B.width};

    if (point_in_poly_large(eh, ip)) {
      enemy_head_inside = true;
    } else {
      float d1 = dist2(eh.x, eh.y, target_pt.x, target_pt.y);
      float d2_ = dist2(ea.x, ea.y, target_pt.x, target_pt.y);
      float dm = d1 < d2_ ? d1 : d2_;
      if (dm < enemy_head_d2) enemy_head_d2 = dm;
    }

    bool offset_set = false;
    float offset = 0.0f;
    poly_box cpolbody;
    float body_offset = (hug ? HUG_BODY_MULT : ENEMY_BODY_MULT) * (B.width + ew);

    int pn = tdarray_length(sk->pts);
    for (int j = 0; j < pn; j++) {
      body_part* po = sk->pts + j;
      if (po->dying) continue;
      v2 pt = {po->xx + po->fx, po->yy + po->fy};
      if (point_in_poly_large(pt, ip)) continue;

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

  float tail_behind = B.bpts_len - close_t;
  v2 ft = unit_vec((v2){B.follow_tx - ax, B.follow_ty - ay});
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
  v2 goal = {ax + goal_dir.x * 4.0f * B.width,
             ay + goal_dir.y * 4.0f * B.width};

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

  {
    const float grd = gdata->data.grd;
    if (grd > 0.0f && B.wall_dist_grd < WALL_RANGE) {
      float ox = B.x - grd, oy = B.y - grd;
      float od = sqrtf(ox * ox + oy * oy);
      if (od > 1.0f) {
        float ix = -ox / od, iy = -oy / od;
        float cr = B.ca * iy - B.sa * ix;
        B.circle_dir = cr > 0.0f ? -1 : +1;
      }
    }
  }

  gdata->bot.output.accel = (bool)B.default_accel;
  B.goal = heading_rel(((float)B.circle_dir * (float)M_PI) / (float)div);

  circ tail = {me->pts[first].xx, me->pts[first].yy,
               (float)(int)(roundf(B.head_circle.r) / 2.0f)};
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
  B.fx = me->fx;
  B.fy = me->fy;
  B.ang = me->ang;
  B.aang = me->ehang;
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
  float nkx = B.x, nky = B.y;
  int mpn = tdarray_length(me->pts);
  if (mpn > 0) {
    nkx = me->pts[mpn - 1].xx;
    nky = me->pts[mpn - 1].yy;
  }
  B.side_r = (circ){nkx - B.sa * B.width, nky + B.ca * B.width,
                    B.width * B.speed_mult};
  B.side_l = (circ){nkx + B.sa * B.width, nky - B.ca * B.width,
                    B.width * B.speed_mult};

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
  bool unsafe_course = clearance <= guard && outward_heading > -0.25f;
  if (B.stage != 0) {
    float hard_clear = fmaxf(150.0f, B.width * 1.6f);
    imminent = clearance <= hard_clear;
    unsafe_course = false;
  }
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
  memset(&S, 0, sizeof(S));
  g_inside_valid = false;
  S.enc_range = 800.0f;
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
  (void)usrs->bot_radius_mult;
  B.follow_circle_length = usrs->bot_follow_circle_score;

  g_bot_dbg.avoid_active   = false;
  g_bot_dbg.encircle_state = 0;

  if (tdarray_length(gdata->data.snakes) == 0) return;

  every(gdata);
  safety_gather(gdata);
  field_build(gdata);
  timer_fire(gdata);

  const double now = now_ms();
  if (B.forage_until > 0.0 && now >= B.forage_until) {
    B.forage_until = 0.0;
    B.forage_cool = now + FORAGE_COOL_MS;
  }
  bool foraging = B.forage_until > 0.0;

  if (SBOT_FORAGE && B.enable_follow_circle &&
      B.snake_len >= B.follow_circle_length && now >= B.forage_check_at) {
    B.forage_check_at = now + FORAGE_CHECK_MS;
    float fx = 0, fy = 0;
    bool food = forage_pick(gdata, &fx, &fy);
    if (foraging) {
      if (food) {
        B.goal = (v2){roundf(fx), roundf(fy)};
      } else {
        B.forage_until = 0.0;
        B.forage_cool = now + FORAGE_COOL_MS;
        foraging = false;
      }
    } else if (B.stage == 2 && food && now >= B.forage_cool &&
               !(B.trapped_until > now)) {
      B.forage_until = now + FORAGE_MS;
      foraging = true;
      B.stage = 0;
      B.has_food = false;
      B.goal = (v2){roundf(fx), roundf(fy)};
    }
  }

  const bool coil_ok = true;
  if (B.enable_follow_circle) {
    if (B.snake_len < B.follow_circle_length || foraging)
      B.stage = 0;
    else if (B.stage == 1 && !coil_ok)
      B.stage = 0;
    else if (B.stage != 2 && coil_ok)
      to_circle(gdata);
  } else {
    B.stage = 2;
  }
  if (B.has_food && B.stage != 0) B.has_food = false;

#if SBOT_BORDER_EMERGENCY
  if (avoid_border_emergency(gdata)) {
  } else
#endif
  if (B.stage == 2) {
    bot->output.accel = (bool)B.default_accel;
    follow_circle_self(gdata);
  } else if (check_collision(gdata) || check_encircle(gdata)) {
    if (B.timer_pending) B.timer_at = now_ms() + COLLISION_MS;
  } else {
    if (B.snake_len > B.follow_circle_length && coil_ok && !foraging)
      B.stage = 1;
    if (!B.timer_pending) {
      B.timer_pending = true;
      B.timer_at = now_ms() + ACTION_MS;
    }
    bot->output.accel = (bool)B.default_accel;
  }
  if (B.stage != 2) g_inside_valid = false;

  if (safety_steer()) bot->output.accel = true;
  if (bot->output.accel && !boost_lane_ok(gdata)) bot->output.accel = false;

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
