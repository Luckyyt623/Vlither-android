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
void ntl_voice_panel(tenv* env);
bool ntl_team_voice_controls_open(void);
void ntl_team_handle_voice_key(void);
void ntl_team_destroy(tenv* env);
void ntl_team_system_message(const char* text);
void ntl_team_trigger_sos(void);
bool ntl_team_local_sos_active(void);
int ntl_team_leaderboard_status(const char* nickname, const char* server);
bool ntl_team_send_text(const char* text);
int ntl_team_tag_for_snake(uint16_t ntl_id, const char* server);
bool ntl_team_is_snake_teammate(uint16_t ntl_id, const char* server);
#endif
