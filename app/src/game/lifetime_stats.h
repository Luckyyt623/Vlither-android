#ifndef LIFETIME_STATS_H
#define LIFETIME_STATS_H

void lifetime_stats_load(void);
int lifetime_stats_best_length(void);
int lifetime_stats_total_kills(void);
int lifetime_stats_best_match_kills(void);

void lifetime_stats_note_length(int length);
void lifetime_stats_add_kill(void);
void lifetime_stats_note_match_kills(int kills);
void lifetime_stats_save_if_dirty(void);

#endif
