#ifndef OEF_H
#define OEF_H

#include <thermite.h>

#include "game_data.h"

void time_step(tenv* env);
void oef(tenv* env);

void ping_mark_sent(game_data* gdata);
void ping_note_poll(game_data* gdata);
void ping_mark_received(game_data* gdata);

#endif
