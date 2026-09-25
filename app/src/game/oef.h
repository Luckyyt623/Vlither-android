#ifndef OEF_H
#define OEF_H

#include <thermite.h>

#include "game_data.h"

void time_step(tenv* env);
void oef(tenv* env);

/* NTL ping system.
   Call ping_mark_sent() right when the ping request (251) is sent and
   ping_mark_received() when the pong ('p') arrives.
     ping      = current ping. Hard-set to the newest RTT once per second,
                 otherwise averaged with it: ping = (ping + rtt) / 2.
     ping_peak = worst RTT seen in the current 10 s window.
   Both are shown as "ping(peak) ms", exactly like NTL. */
void ping_mark_sent(game_data* gdata);
/* Call right before every network poll so a late pong can be told apart from
   a slow network (see ping_mark_received). */
void ping_note_poll(game_data* gdata);
void ping_mark_received(game_data* gdata);

#endif
