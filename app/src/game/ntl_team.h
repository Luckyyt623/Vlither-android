#ifndef NTL_TEAM_H
#define NTL_TEAM_H

#include <thermite.h>

void ntl_team_init(tenv* env);
void ntl_team_update(tenv* env);
void ntl_team_draw(tenv* env);
void ntl_team_draw_minimap(tenv* env, float x, float y, float size);
void ntl_team_consume_ui_touch(tenv* env);
void ntl_team_panel(tenv* env);
void ntl_team_destroy(tenv* env);

#endif
