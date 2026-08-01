#ifndef KEY_BUTTONS_H
#define KEY_BUTTONS_H

#include <thermite.h>

void ui_key_buttons_init(tenv* env);
void ui_key_buttons(tenv* env);
void ui_key_buttons_destroy(tenv* env);
void ui_key_buttons_open_editor(tenv* env);

#ifdef ANDROID
/* Native Android touch path for custom buttons. This bypasses ImGui/HUD
   ownership so buttons placed at the top edge cannot lose their tap. */
bool ui_key_buttons_native_press(float x, float y, int pointer_id);
bool ui_key_buttons_native_release(int pointer_id);
void ui_key_buttons_native_cancel_all(void);
#endif

#endif
