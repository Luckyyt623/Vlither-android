#include "vlither_ratings.h"

#include "vlither_tags.h"
#include "user_settings.h"
#include "../external/mongoose.h"
#include "../user.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VLITHER_RATING_MAX_ITEMS 40
#define VLITHER_RATING_HTTP_TIMEOUT_SECONDS 15.0

typedef enum rating_http_kind {
  RATING_HTTP_NONE = 0,
  RATING_HTTP_LOAD,
  RATING_HTTP_SUBMIT,
  RATING_HTTP_REPORT
} rating_http_kind;

typedef struct vlither_rating_item {
  char id[65];
  char nickname[64];
  char message[VLITHER_RATING_MESSAGE_MAX + 1];
  int rating;
  long long updated_at_ms;
  bool mine;
} vlither_rating_item;

static struct {
  bool ready;
  bool loaded;
  bool refresh_pending;
  tenv *env;
  struct mg_mgr mgr;
  struct mg_connection *http;
  rating_http_kind http_kind;
  double http_started;
  char http_url[512];
  char pending_body[1400];
  char status[224];
  vlither_rating_item items[VLITHER_RATING_MAX_ITEMS];
  int count;
  double average;
  int total;
  int mine_index;
} R;

static void set_status(const char *text) {
  strncpy(R.status, text ? text : "", sizeof R.status - 1);
  R.status[sizeof R.status - 1] = 0;
}

static bool owner_token_valid(const char *token) {
  if (!token || token[64] != 0) return false;
  for (int i = 0; i < 64; ++i)
    if (!isxdigit((unsigned char)token[i])) return false;
  return true;
}

static void ensure_owner_token(user_settings *us) {
  if (!us || owner_token_valid(us->ratings_owner_token)) return;
  unsigned char bytes[32] = {0};
  if (!mg_random(bytes, sizeof bytes)) {
    unsigned long long fallback =
        (unsigned long long)mg_millis() ^ (unsigned long long)(uintptr_t)us;
    for (int i = 0; i < 32; ++i) {
      fallback = fallback * 6364136223846793005ULL + 1442695040888963407ULL;
      bytes[i] = (unsigned char)(fallback >> 32);
    }
  }
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    us->ratings_owner_token[i * 2] = hex[(bytes[i] >> 4) & 15];
    us->ratings_owner_token[i * 2 + 1] = hex[bytes[i] & 15];
  }
  us->ratings_owner_token[64] = 0;
  save_user_settings(us);
}

static void json_escape(char *dst, size_t dst_size, const char *src) {
  size_t n = 0;
  if (!dst || !dst_size) return;
  for (; src && *src && n + 1 < dst_size; ++src) {
    unsigned char ch = (unsigned char)*src;
    const char *escape = NULL;
    if (ch == '"') escape = "\\\"";
    else if (ch == '\\') escape = "\\\\";
    else if (ch == '\n') escape = "\\n";
    else if (ch == '\r') escape = "\\r";
    else if (ch == '\t') escape = "\\t";
    if (escape) {
      if (n + 2 >= dst_size) break;
      dst[n++] = escape[0]; dst[n++] = escape[1];
    } else if (ch >= 32) {
      dst[n++] = (char)ch;
    }
  }
  dst[n] = 0;
}

static void copy_json_string(struct mg_str json, const char *path,
                             char *out, size_t out_size,
                             const char *fallback) {
  char *value = mg_json_get_str(json, path);
  const char *source = value ? value : (fallback ? fallback : "");
  strncpy(out, source, out_size - 1);
  out[out_size - 1] = 0;
  if (value) mg_free(value);
}

static long long json_millis(struct mg_str json, const char *path) {
  double value = 0.0;
  if (!mg_json_get_num(json, path, &value) || !isfinite(value) || value < 0.0)
    return 0;
  return (long long)value;
}

static void parse_reviews(struct mg_str json) {
  memset(R.items, 0, sizeof R.items);
  R.count = 0;
  R.mine_index = -1;
  R.average = 0.0;
  mg_json_get_num(json, "$.summary.average", &R.average);
  if (!isfinite(R.average) || R.average < 0.0 || R.average > 5.0) R.average = 0.0;
  R.total = (int)mg_json_get_long(json, "$.summary.total", 0);
  if (R.total < 0) R.total = 0;

  for (int i = 0; i < VLITHER_RATING_MAX_ITEMS; ++i) {
    char base[64], path[96];
    snprintf(base, sizeof base, "$.reviews[%d]", i);
    snprintf(path, sizeof path, "%s.id", base);
    char *id = mg_json_get_str(json, path);
    if (!id) break;
    vlither_rating_item *item = &R.items[R.count++];
    strncpy(item->id, id, sizeof item->id - 1);
    mg_free(id);
    snprintf(path, sizeof path, "%s.nickname", base);
    copy_json_string(json, path, item->nickname, sizeof item->nickname,
                     "Vlither Player");
    snprintf(path, sizeof path, "%s.message", base);
    copy_json_string(json, path, item->message, sizeof item->message, "");
    snprintf(path, sizeof path, "%s.rating", base);
    item->rating = (int)mg_json_get_long(json, path, 0);
    if (item->rating < 1 || item->rating > 5) item->rating = 0;
    snprintf(path, sizeof path, "%s.updatedAtMs", base);
    item->updated_at_ms = json_millis(json, path);
    snprintf(path, sizeof path, "%s.mine", base);
    mg_json_get_bool(json, path, &item->mine);
    if (item->mine) R.mine_index = R.count - 1;
  }
  R.loaded = true;
  char text[160];
  snprintf(text, sizeof text, R.total == 1
      ? "Showing 1 player review." : "Showing %d player reviews.", R.total);
  set_status(text);
}

static void http_cb(struct mg_connection *c, int ev, void *ev_data) {
  if (c != R.http) return;
  if (ev == MG_EV_CONNECT) {
    if (c->is_tls) {
      struct mg_tls_opts tls = {
          .name = mg_url_host(R.http_url), .skip_verification = 1};
      mg_tls_init(c, &tls);
    }
    struct mg_str host = mg_url_host(R.http_url);
    const char *uri = mg_url_uri(R.http_url);
    if (R.http_kind == RATING_HTTP_LOAD) {
      mg_printf(c,
                "GET %s HTTP/1.1\r\nHost: %.*s\r\n"
                "User-Agent: Vlither/Ratings\r\nAccept: application/json\r\n"
                "Connection: close\r\n\r\n",
                uri, (int)host.len, host.buf);
    } else {
      const char *method = R.http_kind == RATING_HTTP_SUBMIT ? "PUT" : "POST";
      mg_printf(c,
                "%s %s HTTP/1.1\r\nHost: %.*s\r\n"
                "User-Agent: Vlither/Ratings\r\nAccept: application/json\r\n"
                "Content-Type: application/json\r\nContent-Length: %d\r\n"
                "Connection: close\r\n\r\n%s",
                method, uri, (int)host.len, host.buf,
                (int)strlen(R.pending_body), R.pending_body);
    }
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    int http_status = mg_http_status(hm);
    bool ok = false;
    mg_json_get_bool(hm->body, "$.ok", &ok);
    if (http_status >= 200 && http_status < 300 && ok) {
      if (R.http_kind == RATING_HTTP_LOAD) {
        parse_reviews(hm->body);
      } else if (R.http_kind == RATING_HTTP_SUBMIT) {
        char *visibility = mg_json_get_str(hm->body, "$.status");
        set_status(visibility && !strcmp(visibility, "hidden")
          ? "Review saved. It is currently hidden by moderation."
          : "Your review is published.");
        if (visibility) mg_free(visibility);
        R.refresh_pending = true;
      } else {
        bool already = false;
        mg_json_get_bool(hm->body, "$.alreadyReported", &already);
        set_status(already ? "You already reported this review."
                           : "Review reported for moderation.");
        R.refresh_pending = true;
      }
    } else {
      char *error = mg_json_get_str(hm->body, "$.error");
      char text[224];
      if (error && error[0])
        snprintf(text, sizeof text, "%s", error);
      else
        snprintf(text, sizeof text, "Ratings request failed (HTTP %d).",
                 http_status);
      set_status(text);
      if (error) mg_free(error);
    }
    R.http = NULL;
    R.http_kind = RATING_HTTP_NONE;
    R.http_started = 0;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR || ev == MG_EV_CLOSE) {
    if (R.http == c) {
      R.http = NULL;
      R.http_kind = RATING_HTTP_NONE;
      R.http_started = 0;
      set_status("Ratings service is unavailable. Check your connection.");
    }
  }
}

static bool begin_http(rating_http_kind kind, const char *url,
                       const char *body) {
  if (!R.ready || R.http || !url || !url[0]) return false;
  strncpy(R.http_url, url, sizeof R.http_url - 1);
  R.http_url[sizeof R.http_url - 1] = 0;
  if (body) {
    strncpy(R.pending_body, body, sizeof R.pending_body - 1);
    R.pending_body[sizeof R.pending_body - 1] = 0;
  } else R.pending_body[0] = 0;
  R.http_kind = kind;
  R.http = mg_http_connect(&R.mgr, R.http_url, http_cb, NULL);
  R.http_started = mg_millis() / 1000.0;
  if (!R.http) {
    R.http_kind = RATING_HTTP_NONE;
    R.http_started = 0;
    set_status("Could not start ratings request.");
    return false;
  }
  return true;
}

void vlither_ratings_refresh(void) {
  if (!R.ready || !R.env || !R.env->usr) return;
  if (R.http) { R.refresh_pending = true; return; }
  const char *client = R.env->usr->usrs.ntl_client_id;
  char encoded[96];
  if (!mg_url_encode(client, strlen(client), encoded, sizeof encoded)) return;
  char url[512];
  int n = snprintf(url, sizeof url, "%s/api/v1/reviews?clientId=%s&limit=%d",
                   vlither_backend_base_url(), encoded,
                   VLITHER_RATING_MAX_ITEMS);
  if (n <= 0 || n >= (int)sizeof url) return;
  set_status("Loading player reviews...");
  begin_http(RATING_HTTP_LOAD, url, NULL);
}

bool vlither_ratings_submit(int rating, const char *message) {
  if (!R.ready || !R.env || !R.env->usr || R.http) return false;
  if (rating < 1 || rating > 5) {
    set_status("Choose a rating from 1 to 5.");
    return false;
  }
  while (message && isspace((unsigned char)*message)) ++message;
  if (!message || strlen(message) < 2) {
    set_status("Write at least 2 characters before publishing.");
    return false;
  }
  user_settings *us = &R.env->usr->usrs;
  ensure_owner_token(us);
  char escaped_client[96], escaped_token[160], escaped_nick[128];
  char escaped_message[VLITHER_RATING_MESSAGE_MAX * 2 + 8];
  json_escape(escaped_client, sizeof escaped_client, us->ntl_client_id);
  json_escape(escaped_token, sizeof escaped_token, us->ratings_owner_token);
  json_escape(escaped_nick, sizeof escaped_nick,
              us->nickname[0] ? us->nickname : "Vlither Player");
  json_escape(escaped_message, sizeof escaped_message, message);
  char body[1400];
  int body_len = snprintf(body, sizeof body,
      "{\"clientId\":\"%s\",\"ownerToken\":\"%s\","
      "\"nickname\":\"%s\",\"rating\":%d,\"message\":\"%s\"}",
      escaped_client, escaped_token, escaped_nick, rating, escaped_message);
  if (body_len <= 0 || body_len >= (int)sizeof body) {
    set_status("That review is too long.");
    return false;
  }
  char url[384];
  snprintf(url, sizeof url, "%s/api/v1/reviews", vlither_backend_base_url());
  set_status(R.mine_index >= 0 ? "Updating your review..."
                               : "Publishing your review...");
  return begin_http(RATING_HTTP_SUBMIT, url, body);
}

bool vlither_ratings_report(int index) {
  if (!R.ready || !R.env || !R.env->usr || R.http || index < 0 ||
      index >= R.count || R.items[index].mine || !R.items[index].id[0])
    return false;
  char encoded_id[160], escaped_client[96];
  if (!mg_url_encode(R.items[index].id, strlen(R.items[index].id),
                     encoded_id, sizeof encoded_id)) return false;
  json_escape(escaped_client, sizeof escaped_client,
              R.env->usr->usrs.ntl_client_id);
  char url[512], body[160];
  snprintf(url, sizeof url, "%s/api/v1/reviews/%s/report",
           vlither_backend_base_url(), encoded_id);
  snprintf(body, sizeof body, "{\"clientId\":\"%s\"}", escaped_client);
  set_status("Sending report...");
  return begin_http(RATING_HTTP_REPORT, url, body);
}

void vlither_ratings_init(tenv *env) {
  memset(&R, 0, sizeof R);
  R.env = env;
  R.mine_index = -1;
  mg_mgr_init(&R.mgr);
  R.ready = true;
  if (env && env->usr) ensure_owner_token(&env->usr->usrs);
  set_status("Loading player reviews...");
  vlither_ratings_refresh();
}

void vlither_ratings_update(tenv *env) {
  if (!R.ready) return;
  if (env) R.env = env;
  mg_mgr_poll(&R.mgr, 0);
  double now = mg_millis() / 1000.0;
  if (R.http && R.http_started > 0 &&
      now - R.http_started > VLITHER_RATING_HTTP_TIMEOUT_SECONDS) {
    R.http->is_closing = 1;
    R.http = NULL;
    R.http_kind = RATING_HTTP_NONE;
    R.http_started = 0;
    set_status("Ratings request timed out.");
  }
  if (!R.http && R.refresh_pending) {
    R.refresh_pending = false;
    vlither_ratings_refresh();
  }
}

void vlither_ratings_destroy(tenv *env) {
  (void)env;
  if (!R.ready) return;
  mg_mgr_free(&R.mgr);
  memset(&R, 0, sizeof R);
}

bool vlither_ratings_loading(void) { return R.http != NULL; }
bool vlither_ratings_loaded(void) { return R.loaded; }
const char *vlither_ratings_status(void) { return R.status; }
double vlither_ratings_average(void) { return R.average; }
int vlither_ratings_total(void) { return R.total; }
int vlither_ratings_count(void) { return R.count; }

static const vlither_rating_item *item_at(int index) {
  return index >= 0 && index < R.count ? &R.items[index] : NULL;
}
const char *vlither_ratings_id_at(int index) {
  const vlither_rating_item *item = item_at(index); return item ? item->id : "";
}
const char *vlither_ratings_nickname_at(int index) {
  const vlither_rating_item *item = item_at(index); return item ? item->nickname : "";
}
const char *vlither_ratings_message_at(int index) {
  const vlither_rating_item *item = item_at(index); return item ? item->message : "";
}
int vlither_ratings_value_at(int index) {
  const vlither_rating_item *item = item_at(index); return item ? item->rating : 0;
}
long long vlither_ratings_updated_at_ms(int index) {
  const vlither_rating_item *item = item_at(index); return item ? item->updated_at_ms : 0;
}
bool vlither_ratings_is_mine_at(int index) {
  const vlither_rating_item *item = item_at(index); return item && item->mine;
}
bool vlither_ratings_has_mine(void) { return R.mine_index >= 0; }
int vlither_ratings_mine_value(void) {
  return R.mine_index >= 0 ? R.items[R.mine_index].rating : 0;
}
const char *vlither_ratings_mine_message(void) {
  return R.mine_index >= 0 ? R.items[R.mine_index].message : "";
}
