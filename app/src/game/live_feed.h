#ifndef LIVE_FEED_H
#define LIVE_FEED_H

#include <stdbool.h>
#include <stdint.h>
#include <thermite.h>

#ifndef VLITHER_LIVE_RELAY_URL
#define VLITHER_LIVE_RELAY_URL "ws://YOUR-RELAY-HOST:8090/live"
#endif

#define LIVE_DOT_NONE  0
#define LIVE_DOT_SHAPE 1
#define LIVE_DOT_TEXT  2

void live_feed_init(tenv* env);
void live_feed_update(tenv* env);
void live_feed_destroy(tenv* env);

bool live_feed_active(void);
bool live_feed_has_text(void);

int live_feed_dot_kind(float x, float y);

void live_feed_draw(tenv* env, float view_x, float view_y, float zoom,
                    float half_w, float half_h, int my_snake_id,
                    bool draw_lines, bool draw_text);

const char* live_feed_status(void);

#endif
