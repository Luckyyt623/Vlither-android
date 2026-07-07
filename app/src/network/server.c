#include "server.h"

#include "../user.h"
#include "callback.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

void server_init(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;
  mg_log_set(MG_LL_NONE);
  mg_mgr_init(&gdata->network_manager);
}

void server_connect(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
  user_settings* usrs = &usr->usrs;

  char url[256] = {};

  bool is_local = (strncmp(usrs->ipv4, "127.", 4) == 0 ||
                   strncmp(usrs->ipv4, "localhost", 9) == 0 ||
                   strncmp(usrs->ipv4, "192.168.", 8) == 0 ||
                   strncmp(usrs->ipv4, "10.", 3) == 0);
  sprintf(url, "%s://%s/slither", is_local ? "ws" : "wss", usrs->ipv4);
  gdata->connection =
    mg_ws_connect(&gdata->network_manager, url, server_callback, env,
                  "%s:%s\r\n%s:%s\r\n",
                  "Origin", "https://slither.com",
                  "Host", "slither.com");
#ifdef ANDROID

  mg_mgr_poll(&gdata->network_manager, 5);
  mg_mgr_poll(&gdata->network_manager, 5);
#endif
}

void server_poll(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;
#ifdef ANDROID
  mg_mgr_poll(&gdata->network_manager, 5);
#else
  mg_mgr_poll(&gdata->network_manager, 0);
#endif
}

void server_destroy(tenv* env) {
  tuser_data* usr = env->usr;
  game_data* gdata = &usr->gdata;

  mg_mgr_free(&gdata->network_manager);
}

#define SL_URL_HTTP  "http://slither.io/i80124.txt"
#define SL_URL_HTTPS "https://slither.io/i80124.txt"

static const char* CUSTOM_SERVER_IPS[CUSTOM_SERVER_COUNT] = {
  "206.206.76.190:444",
  "139.84.166.84:444",
  "51.91.19.175:444",
  "206.221.176.241:444",
};
const char* CUSTOM_SERVER_NAMES[CUSTOM_SERVER_COUNT] = {
  "Singapore Battledome",
  "India Battledome",
  "EU Battledome",
  "USA Battledome",
};

static void server_list_seed_custom(game_data* gdata) {
  gdata->server_list.count = 0;
  for (int i = 0; i < CUSTOM_SERVER_COUNT; i++) {
    strncpy(gdata->server_list.ips[i], CUSTOM_SERVER_IPS[i], MAX_SERVER_IP_LEN);
    gdata->server_list.ips[i][MAX_SERVER_IP_LEN] = '\0';
    gdata->server_list.count++;
  }
  gdata->server_list.custom_count = gdata->server_list.count;
}

static void server_list_parse(const char* data, int len,
                               char ips[][MAX_SERVER_IP_LEN + 1], int* count) {

  enum { MAX_HB = MAX_SERVER_LIST * 28 + 28 };
  static int hb[MAX_SERVER_LIST * 28 + 28];
  int hb_len = 0;
  int cb = 0, eb = 0, gb = 0;

  for (int i = 1; i < len && hb_len < MAX_HB; i++) {
    int ch = (unsigned char)data[i];
    int bb = (((ch - 97 - eb) % 26) + 26) % 26;
    cb = cb * 16 + bb;
    eb = (eb + 7) % 26;
    if (gb == 1) {
      hb[hb_len++] = cb;
      gb = 0;
      cb = 0;
    } else {
      gb++;
    }
  }

  /* NOTE: *count is NOT reset here — the caller may have pre-seeded
     custom server entries at the start of the array, and this function
     appends the fetched official servers right after them. */
  if (hb_len > 0 && hb_len % 28 == 0) {

    for (int i = 0; i + 27 < hb_len && *count < MAX_SERVER_LIST; i += 28) {
      if (hb[i] <= 120) {
        int port = (hb[i + 21] << 8) | hb[i + 22];
        snprintf(ips[*count], MAX_SERVER_IP_LEN + 1,
                 "%d.%d.%d.%d:%d",
                 hb[i+1], hb[i+2], hb[i+3], hb[i+4], port);
        (*count)++;
      }
    }
  } else if (hb_len > 0 && hb_len % 11 == 0) {

    for (int i = 0; i + 10 < hb_len && *count < MAX_SERVER_LIST; i += 11) {
      int port = (hb[i + 5] << 8) | hb[i + 6];
      snprintf(ips[*count], MAX_SERVER_IP_LEN + 1,
               "%d.%d.%d.%d:%d",
               hb[i+0], hb[i+1], hb[i+2], hb[i+3], port);
      (*count)++;
    }
  }
}

static void sl_do_https(game_data* gdata);

static void sl_http_cb(struct mg_connection* c, int ev, void* ev_data) {
  game_data* gdata = (game_data*)c->fn_data;

  if (ev == MG_EV_CONNECT) {
    if (gdata->server_list.retry_https) {
      struct mg_tls_opts tls = {.skip_verification = 1};
      mg_tls_init(c, &tls);
    }
    mg_printf(c,
              "GET /i80124.txt HTTP/1.0\r\n"
              "Host: slither.io\r\n"
              "User-Agent: Mozilla/5.0\r\n"
              "Connection: close\r\n\r\n");
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;
    int status = mg_http_status(hm);
    if (status == 200 && hm->body.len > 0) {
      server_list_parse(hm->body.buf, (int)hm->body.len,
                        gdata->server_list.ips, &gdata->server_list.count);
      gdata->server_list.fetching   = false;
      gdata->server_list.fetched    = true;
      gdata->server_list.fetch_error = false;
    } else if ((status >= 301 && status <= 308) &&
               !gdata->server_list.retry_https) {

      gdata->server_list.retry_https = true;
      sl_do_https(gdata);
    } else if (!gdata->server_list.retry_https) {

      gdata->server_list.retry_https = true;
      sl_do_https(gdata);
    } else {
      gdata->server_list.fetching    = false;
      gdata->server_list.fetch_error = true;
    }
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR) {
    if (!gdata->server_list.retry_https) {

      gdata->server_list.retry_https = true;
      sl_do_https(gdata);
    } else {
      gdata->server_list.fetching    = false;
      gdata->server_list.fetch_error = true;
    }
  } else if (ev == MG_EV_CLOSE) {
    if (gdata->server_list.fetching && !gdata->server_list.retry_https) {
      gdata->server_list.fetching    = false;
      gdata->server_list.fetch_error = true;
    }
  }
}

static void sl_do_https(game_data* gdata) {
  mg_http_connect(&gdata->server_list.mgr, SL_URL_HTTPS, sl_http_cb, gdata);
}

void server_list_init(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  mg_mgr_init(&gdata->server_list.mgr);
  gdata->server_list.fetching    = false;
  gdata->server_list.fetched     = false;
  gdata->server_list.fetch_error = false;
  gdata->server_list.retry_https = false;
  gdata->server_list.pinging     = 0;
  gdata->server_list.ping_stop   = 0;
  gdata->server_list.pings_done  = 0;
  for (int i = 0; i < MAX_SERVER_LIST; i++) {
    gdata->server_list.pings[i]        = -1;
    gdata->server_list.sorted_order[i] = i;
  }
  server_list_seed_custom(gdata);
}

void server_list_fetch(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  if (gdata->server_list.fetching) return;
  gdata->server_list.ping_stop   = 1;
  gdata->server_list.pings_done  = 0;
  gdata->server_list.fetching    = true;
  gdata->server_list.fetched     = false;
  gdata->server_list.fetch_error = false;
  gdata->server_list.retry_https = false;
  for (int i = 0; i < MAX_SERVER_LIST; i++) {
    gdata->server_list.pings[i]        = -1;
    gdata->server_list.sorted_order[i] = i;
  }
  server_list_seed_custom(gdata);
  mg_http_connect(&gdata->server_list.mgr, SL_URL_HTTP, sl_http_cb, gdata);
}

void server_list_poll(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  mg_mgr_poll(&gdata->server_list.mgr, 0);
}

void server_list_destroy(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  gdata->server_list.ping_stop = 1;
  mg_mgr_free(&gdata->server_list.mgr);
}

/* ------------------------------------------------------------------ *
 * Server list ping probe.
 *
 * This mirrors the ping calculation used by the official web client:
 * rather than just timing a raw TCP connect() to the server's game
 * port (which only measures handshake latency and can be skewed by
 * SYN-cookie/accept-queue behavior), it opens a real WebSocket to the
 * server's dedicated ping-probe endpoint on port 80 at "/ptc", sends a
 * single application-level ping byte (0x70), waits for the same byte
 * to be echoed back, and repeats that round trip 4 times. The final
 * reported ping is the *minimum* of those 4 samples, which is what the
 * web client does to filter out one-off jitter/queueing delay and
 * report the best achievable latency to that server.
 * ------------------------------------------------------------------ */

#define PING_BATCH    64
#define PING_TIMEOUT  7000   /* ms, matches the web client's ipv4 probe timeout */
#define PING_SAMPLES  4      /* number of ping/pong round trips sampled */
#define PING_BYTE     0x70   /* 'p' - single-byte ping payload used by /ptc */

typedef struct {
  uint64_t start_ms;
  int      sample_count;
  int      samples[PING_SAMPLES];
  bool     done;
  int      result;
} ping_probe;

static void sl_ping_ws_cb(struct mg_connection* c, int ev, void* ev_data) {
  ping_probe* p = (ping_probe*)c->fn_data;
  if (p->done) return;

  if (ev == MG_EV_WS_OPEN) {
    p->start_ms = mg_millis();
    uint8_t b = PING_BYTE;
    mg_ws_send(c, &b, 1, WEBSOCKET_OP_BINARY);

  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
    if (wm->data.len == 1 && (uint8_t)wm->data.buf[0] == PING_BYTE) {
      int elapsed = (int)(mg_millis() - p->start_ms);
      if (p->sample_count < PING_SAMPLES) p->samples[p->sample_count++] = elapsed;

      if (p->sample_count < PING_SAMPLES) {
        p->start_ms = mg_millis();
        uint8_t b = PING_BYTE;
        mg_ws_send(c, &b, 1, WEBSOCKET_OP_BINARY);
      } else {
        int best = 9999;
        for (int i = 0; i < p->sample_count; i++)
          if (p->samples[i] < best) best = p->samples[i];
        p->result = best;
        p->done   = true;
        c->is_closing = 1;
      }
    }

  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (!p->done) {
      /* Connection dropped before all 4 samples came back. If we got at
         least one good round trip, report the best of what we have
         instead of throwing the whole probe away. */
      int best = 9999;
      for (int i = 0; i < p->sample_count; i++)
        if (p->samples[i] < best) best = p->samples[i];
      p->result = best;
      p->done   = true;
    }
  }
}

static void* sl_ping_thread(void* arg) {
  game_data* gdata = (game_data*)arg;
  int n = gdata->server_list.count;

  for (int bs = 0; bs < n; bs += PING_BATCH) {
    if (gdata->server_list.ping_stop) break;

    int be  = (bs + PING_BATCH < n) ? bs + PING_BATCH : n;
    int bsz = be - bs;

    struct mg_mgr ping_mgr;
    mg_mgr_init(&ping_mgr);

    ping_probe probes[PING_BATCH];
    memset(probes, 0, sizeof(probes));

    for (int j = 0; j < bsz; j++) {
      int idx = bs + j;
      char ip_buf[32] = {0};
      int  port = 0;
      if (sscanf(gdata->server_list.ips[idx], "%31[^:]:%d", ip_buf, &port) != 2) {
        probes[j].done   = true;
        probes[j].result = 9999;
        continue;
      }

      char url[64];
      snprintf(url, sizeof(url), "ws://%s:80/ptc", ip_buf);

      struct mg_connection* c = mg_ws_connect(
        &ping_mgr, url, sl_ping_ws_cb, &probes[j],
        "%s:%s\r\n%s:%s\r\n",
        "Origin", "https://slither.com",
        "Host", "slither.com");

      if (!c) {
        probes[j].done   = true;
        probes[j].result = 9999;
      }
    }

    uint64_t t0 = mg_millis();
    while (!gdata->server_list.ping_stop) {
      bool all_done = true;
      for (int j = 0; j < bsz; j++) {
        if (!probes[j].done) { all_done = false; break; }
      }
      if (all_done) break;
      if (mg_millis() - t0 >= PING_TIMEOUT) break;
      mg_mgr_poll(&ping_mgr, 50);
    }

    for (int j = 0; j < bsz; j++) {
      int idx = bs + j;
      if (!probes[j].done) {
        int best = 9999;
        for (int i = 0; i < probes[j].sample_count; i++)
          if (probes[j].samples[i] < best) best = probes[j].samples[i];
        probes[j].result = best;
      }
      gdata->server_list.pings[idx] = probes[j].result;
      gdata->server_list.pings_done++;
    }

    mg_mgr_free(&ping_mgr);
  }

  if (!gdata->server_list.ping_stop) {
    int n2 = gdata->server_list.count;
    for (int i = 0; i < n2; i++) gdata->server_list.sorted_order[i] = i;
    for (int i = 1; i < n2; i++) {
      int ki = gdata->server_list.sorted_order[i];
      int kp = gdata->server_list.pings[ki];
      int j  = i - 1;
      while (j >= 0 && gdata->server_list.pings[gdata->server_list.sorted_order[j]] > kp) {
        gdata->server_list.sorted_order[j+1] = gdata->server_list.sorted_order[j];
        j--;
      }
      gdata->server_list.sorted_order[j+1] = ki;
    }
  }

  gdata->server_list.pinging = 0;
  return NULL;
}

void server_list_start_ping(tenv* env) {
  game_data* gdata = &((tuser_data*)env->usr)->gdata;
  if (gdata->server_list.pinging || gdata->server_list.count == 0) return;
  gdata->server_list.ping_stop  = 0;
  gdata->server_list.pings_done = 0;
  gdata->server_list.pinging    = 1;
  for (int i = 0; i < gdata->server_list.count; i++) {
    gdata->server_list.pings[i]        = -1;
    gdata->server_list.sorted_order[i] = i;
  }
  pthread_t tid;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&tid, &attr, sl_ping_thread, gdata);
  pthread_attr_destroy(&attr);
}
