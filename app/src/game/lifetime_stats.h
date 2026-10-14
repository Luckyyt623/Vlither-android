#ifndef LIFETIME_STATS_H
#define LIFETIME_STATS_H

/* Persistent stats: the best length ever reached, the total number of kills
   across every game, and the most kills in a single match. Stored in their
   own small file so the versioned user.dat layout is not affected. */

void lifetime_stats_load(void);
int lifetime_stats_best_length(void);
int lifetime_stats_total_kills(void);
int lifetime_stats_best_match_kills(void);

/* Call whenever the player's current length is known. */
void lifetime_stats_note_length(int length);
/* Call once per confirmed kill. */
void lifetime_stats_add_kill(void);
/* Call with the current match's kill count after each kill. */
void lifetime_stats_note_match_kills(int kills);
/* Writes to disk only if something changed since the last save. */
void lifetime_stats_save_if_dirty(void);

#endif
