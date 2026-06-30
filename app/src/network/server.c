#include "server.h"

#include "../user.h"
#include "callback.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

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

  *count = 0;
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
  gdata->server_list.count       = 0;
  for (int i = 0; i < MAX_SERVER_LIST; i++) {
    gdata->server_list.pings[i]        = -1;
    gdata->server_list.sorted_order[i] = i;
  }
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
  gdata->server_list.count       = 0;
  for (int i = 0; i < MAX_SERVER_LIST; i++) {
    gdata->server_list.pings[i]        = -1;
    gdata->server_list.sorted_order[i] = i;
  }
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

#define PING_BATCH   64
#define PING_TIMEOUT 2000

static void* sl_ping_thread(void* arg) {
  game_data* gdata = (game_data*)arg;
  int n = gdata->server_list.count;

  for (int bs = 0; bs < n; bs += PING_BATCH) {
    if (gdata->server_list.ping_stop) break;

    int be  = (bs + PING_BATCH < n) ? bs + PING_BATCH : n;
    int bsz = be - bs;

    int        fds[PING_BATCH];
    bool       done_arr[PING_BATCH];
    struct timeval t0;

    gettimeofday(&t0, NULL);
    memset(done_arr, 0, sizeof(done_arr));

    for (int j = 0; j < bsz; j++) {
      fds[j] = -1;
      int idx = bs + j;
      char ip_buf[32] = {0};
      int  port = 0;
      if (sscanf(gdata->server_list.ips[idx], "%31[^:]:%d", ip_buf, &port) != 2) {
        gdata->server_list.pings[idx] = 9999;
        gdata->server_list.pings_done++;
        done_arr[j] = true;
        continue;
      }
      fds[j] = socket(AF_INET, SOCK_STREAM, 0);
      if (fds[j] < 0) {
        gdata->server_list.pings[idx] = 9999;
        gdata->server_list.pings_done++;
        done_arr[j] = true;
        continue;
      }
      fcntl(fds[j], F_SETFL, fcntl(fds[j], F_GETFL, 0) | O_NONBLOCK);
      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_port   = htons((uint16_t)port);
      inet_pton(AF_INET, ip_buf, &addr.sin_addr);
      int r = connect(fds[j], (struct sockaddr*)&addr, sizeof(addr));
      if (r == 0) {
        struct timeval now; gettimeofday(&now, NULL);
        long ms = (now.tv_sec - t0.tv_sec)*1000 + (now.tv_usec - t0.tv_usec)/1000;
        gdata->server_list.pings[idx] = (int)ms;
        close(fds[j]); fds[j] = -1;
        done_arr[j] = true;
        gdata->server_list.pings_done++;
      }
    }

    while (!gdata->server_list.ping_stop) {
      struct timeval now; gettimeofday(&now, NULL);
      long elapsed = (now.tv_sec - t0.tv_sec)*1000 + (now.tv_usec - t0.tv_usec)/1000;
      if (elapsed >= PING_TIMEOUT) break;

      fd_set wfds, efds;
      FD_ZERO(&wfds); FD_ZERO(&efds);
      int max_fd = -1; bool all_done = true;
      for (int j = 0; j < bsz; j++) {
        if (!done_arr[j] && fds[j] >= 0) {
          FD_SET(fds[j], &wfds); FD_SET(fds[j], &efds);
          if (fds[j] > max_fd) max_fd = fds[j];
          all_done = false;
        }
      }
      if (all_done || max_fd < 0) break;

      struct timeval tv = {0, 50000};
      if (select(max_fd + 1, NULL, &wfds, &efds, &tv) > 0) {
        gettimeofday(&now, NULL);
        long ms = (now.tv_sec - t0.tv_sec)*1000 + (now.tv_usec - t0.tv_usec)/1000;
        for (int j = 0; j < bsz; j++) {
          if (done_arr[j] || fds[j] < 0) continue;
          if (FD_ISSET(fds[j], &wfds) || FD_ISSET(fds[j], &efds)) {
            int err = 0; socklen_t sl2 = sizeof(err);
            getsockopt(fds[j], SOL_SOCKET, SO_ERROR, &err, &sl2);
            int idx = bs + j;
            gdata->server_list.pings[idx] = (err == 0) ? (int)ms : 9999;
            close(fds[j]); fds[j] = -1;
            done_arr[j] = true;
            gdata->server_list.pings_done++;
          }
        }
      }
    }

    for (int j = 0; j < bsz; j++) {
      if (fds[j] >= 0) {
        close(fds[j]);
        int idx = bs + j;
        if (gdata->server_list.pings[idx] < 0) gdata->server_list.pings[idx] = 9999;
        gdata->server_list.pings_done++;
      }
    }
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
