#ifndef VLITHER_RATINGS_H
#define VLITHER_RATINGS_H

#include <stdbool.h>
#include <thermite.h>

#define VLITHER_RATING_MESSAGE_MAX 400
#define VLITHER_RATING_REPLY_MAX 12
#define VLITHER_RATING_REPLY_TEXT_MAX 400

void vlither_ratings_init(tenv *env);
void vlither_ratings_update(tenv *env);
void vlither_ratings_destroy(tenv *env);

void vlither_ratings_refresh(void);
bool vlither_ratings_submit(int rating, const char *message);
bool vlither_ratings_report(int index);
bool vlither_ratings_reply(const char *message);

bool vlither_ratings_loading(void);
bool vlither_ratings_loaded(void);
const char *vlither_ratings_status(void);
double vlither_ratings_average(void);
int vlither_ratings_total(void);
int vlither_ratings_count(void);
const char *vlither_ratings_id_at(int index);
const char *vlither_ratings_nickname_at(int index);
const char *vlither_ratings_message_at(int index);
int vlither_ratings_value_at(int index);
long long vlither_ratings_updated_at_ms(int index);
bool vlither_ratings_is_mine_at(int index);
bool vlither_ratings_has_mine(void);
int vlither_ratings_mine_value(void);
const char *vlither_ratings_mine_message(void);

int vlither_ratings_reply_count_at(int index);
const char *vlither_ratings_reply_role_at(int index, int reply_index);
const char *vlither_ratings_reply_message_at(int index, int reply_index);
long long vlither_ratings_reply_time_ms_at(int index, int reply_index);
bool vlither_ratings_can_reply_at(int index);
bool vlither_ratings_has_unread_admin_reply(void);
void vlither_ratings_mark_admin_replies_seen(void);

#endif
