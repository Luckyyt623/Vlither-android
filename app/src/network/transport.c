#include "transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user.h"
#include "callback.h"
#ifdef ANDROID
#include "../android_path.h"
#endif

#define RAW_MAGIC 0x52
#define RAW_MAX_PACKET (1 << 20)

/* Lives in mg_connection::data (32 bytes of application space). */
typedef struct raw_state {
  uint8_t magic;
  uint8_t hbc;       /* length prefix size: 4, then 2 after the 'a' packet */
  uint8_t riddle;    /* waiting for the server's first packet */
} raw_state;

/* ------------------------------------------------------------------------
 * Homepage choice, kept in client_mode.dat (one byte).
 * ---------------------------------------------------------------------- */
static int s_mode = CLIENT_WEB;
static bool s_loaded = false;

static FILE* mode_open(const char* rw) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, (int)sizeof path, "client_mode.dat");
  return fopen(path, rw);
#else
  return fopen("client_mode.dat", rw);
#endif
}

int net_client_mode(void) {
  if (!s_loaded) {
    s_loaded = true;
    FILE* f = mode_open("rb");
    if (f) {
      int v = fgetc(f);
      if (v == CLIENT_MOBILE) s_mode = CLIENT_MOBILE;
      fclose(f);
    }
  }
  return s_mode;
}

void net_set_client_mode(int mode) {
  net_client_mode();
  s_mode = mode == CLIENT_MOBILE ? CLIENT_MOBILE : CLIENT_WEB;
  FILE* f = mode_open("wb");
  if (f) {
    fputc(s_mode, f);
    fclose(f);
  }
}

/* ------------------------------------------------------------------------ */

static raw_state* raw_of(const struct mg_connection* c) {
  return (raw_state*)(void*)((struct mg_connection*)c)->data;
}

bool net_is_raw(const struct mg_connection* c) {
  return c && !c->is_websocket && raw_of(c)->magic == RAW_MAGIC;
}

void net_send(struct mg_connection* c, const void* buf, size_t len) {
  if (!c) return;
  if (!net_is_raw(c)) {
    mg_ws_send(c, buf, len, WEBSOCKET_OP_BINARY);
    return;
  }
  if (len >= RAW_MAX_PACKET) return;
  uint8_t hdr[4];
  int hbc = raw_of(c)->hbc;
  if (hbc == 4) {
    hdr[0] = (uint8_t)(len >> 24);
    hdr[1] = (uint8_t)(len >> 16);
    hdr[2] = (uint8_t)(len >> 8);
    hdr[3] = (uint8_t)len;
  } else {
    hdr[0] = (uint8_t)(len >> 8);
    hdr[1] = (uint8_t)len;
  }
  mg_send(c, hdr, (size_t)hbc);
  mg_send(c, buf, len);
}

struct mg_connection* net_connect_raw(struct mg_mgr* mgr, const char* address,
                                      mg_event_handler_t fn, void* fn_data) {
  char host[128] = {0};
  int port = 0;
  if (!address || !address[0]) return NULL;
  if (address[0] == '[') {
    const char* end = strchr(address, ']');
    if (!end || end[1] != ':' || sscanf(end + 2, "%d", &port) != 1) return NULL;
    size_t n = (size_t)(end - address - 1);
    if (n >= sizeof host - 2) return NULL;
    host[0] = '[';
    memcpy(host + 1, address + 1, n);
    host[n + 1] = ']';
  } else {
    const char* colon = strrchr(address, ':');
    if (!colon || sscanf(colon + 1, "%d", &port) != 1) return NULL;
    size_t n = (size_t)(colon - address);
    if (n == 0 || n >= sizeof host) return NULL;
    memcpy(host, address, n);
  }
  char url[256];
  snprintf(url, sizeof url, "tcp://%s:%d", host, port + 10);
  struct mg_connection* c = mg_connect(mgr, url, fn, fn_data);
  if (c) {
    raw_state* st = raw_of(c);
    memset(st, 0, sizeof *st);
    st->magic = RAW_MAGIC;
    st->hbc = 4;
  }
  return c;
}

/* ---- Android handshake -------------------------------------------------- */

static uint32_t crc32_ieee(const uint8_t* d, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc ^ 0xFFFFFFFFu;
}

/* The Android client answers the server's first packet with 8 letters built
   from the CRC32 of (byte[i + 1] + 3 * i) & 255, i = 0 .. len - 2. */
static void mobile_riddle_answer(const uint8_t* p, int len, uint8_t out[8]) {
  uint8_t* t = malloc((size_t)len);
  if (!t) {
    memset(out, 'A', 8);
    return;
  }
  for (int i = 0; i < len - 1; i++) t[i] = (uint8_t)(p[i + 1] + i * 3);
  uint32_t crc = crc32_ieee(t, (size_t)(len - 1));
  free(t);
  int k = 12;
  for (int i = 0; i < 4; i++) {
    int b = (crc >> (24 - 8 * i)) & 255;
    int hi = b / 16, lo = b % 16;
    out[i * 2] = (uint8_t)(65 + ((rand() & 1) ? 32 : 0) + (hi + k) % 26);
    k += 11;
    out[i * 2 + 1] = (uint8_t)(65 + ((rand() & 1) ? 32 : 0) + (lo + k) % 26);
    k += 11;
  }
}

void net_raw_connected(tenv* env, struct mg_connection* c) {
  (void)env;
  /* Same two packets the Android client sends first. */
  net_send(c, (uint8_t[]){1}, 1);
  /* 99, 1, platform (2 = Android), control mode 0, boost 0, flipped 0,
     sharpness 0, high quality 1, map top left 0, bg name length 0,
     look ahead 0, save nick 0. */
  uint8_t cp[] = {99, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0};
  net_send(c, cp, sizeof cp);
  raw_of(c)->riddle = 1;
}

void net_raw_on_init(struct mg_connection* c) {
  if (net_is_raw(c)) raw_of(c)->hbc = 2;
}

void net_raw_read(tenv* env, struct mg_connection* c) {
  tuser_data* usr = env->usr;
  raw_state* st = raw_of(c);
  for (;;) {
    size_t hbc = st->hbc;
    if (c->recv.len < hbc) break;
    const uint8_t* b = c->recv.buf;
    size_t len = hbc == 4
                     ? ((size_t)b[0] << 24 | (size_t)b[1] << 16 |
                        (size_t)b[2] << 8 | (size_t)b[3])
                     : ((size_t)b[0] << 8 | (size_t)b[1]);
    if (len >= RAW_MAX_PACKET) {
      c->is_closing = 1;
      return;
    }
    if (c->recv.len < hbc + len) break;

    usr->gdata.network_rx_bytes += len;
    if (len > 0) {
      /* copy: the handlers may take a while and the buffer is trimmed after */
      uint8_t* pkt = malloc(len);
      if (!pkt) {
        c->is_closing = 1;
        return;
      }
      memcpy(pkt, c->recv.buf + hbc, len);
      mg_iobuf_del(&c->recv, 0, hbc + len);
      if (st->riddle && len >= 4) {
        st->riddle = 0;
        uint8_t ans[8];
        mobile_riddle_answer(pkt, (int)len, ans);
        net_send(c, ans, sizeof ans);
        send_login_packet(env, c);
      } else {
        net_dispatch(env, pkt, (int)len);
      }
      free(pkt);
    } else {
      mg_iobuf_del(&c->recv, 0, hbc);
    }
    if (c->is_closing) return;
  }
}
