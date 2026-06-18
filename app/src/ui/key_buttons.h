#ifndef KEY_BUTTONS_H
#define KEY_BUTTONS_H

#include <thermite.h>

void ui_key_buttons_init(tenv* env);
void ui_key_buttons(tenv* env);      /* call every frame during PLAYING + TITLE_SCREEN */
void ui_key_buttons_destroy(tenv* env);

#endif
