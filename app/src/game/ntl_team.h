#ifndef NTL_TEAM_H
#define NTL_TEAM_H

#include <stdbool.h>
#include <stdint.h>
#include <thermite.h>

void ntl_team_init(tenv* env);
void ntl_team_update(tenv* env);
void ntl_team_draw(tenv* env);
void ntl_team_draw_minimap(tenv* env, float x, float y, float size);
void ntl_team_consume_ui_touch(tenv* env);
void ntl_team_panel(tenv* env);
void ntl_team_destroy(tenv* env);
void ntl_team_system_message(const char* text);
bool ntl_team_send_text(const char* text);
int ntl_team_tag_for_snake(uint16_t ntl_id, const char* server);

#endif
