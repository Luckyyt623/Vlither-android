#ifndef VLITHER_RATINGS_H
#define VLITHER_RATINGS_H

#include <stdbool.h>
#include <thermite.h>

#define VLITHER_RATING_MESSAGE_MAX 400

void vlither_ratings_init(tenv *env);
void vlither_ratings_update(tenv *env);
void vlither_ratings_destroy(tenv *env);

void vlither_ratings_refresh(void);
bool vlither_ratings_submit(int rating, const char *message);
bool vlither_ratings_report(int index);

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

#endif
