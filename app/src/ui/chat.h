#ifndef CHAT_H
#define CHAT_H

#include <thermite.h>

/* Lifecycle */
void ui_chat_init(tenv* env);
void ui_chat_destroy(tenv* env);

/* Per-frame render – call from the imgui overlay window */
void ui_chat(tenv* env);

/* Add a message to the history (thread-safe from game thread) */
void ui_chat_add_message(const char* sender, const char* text);

/* Toggle active/inactive (called by the tap button in ui_overlay.c) */
void ui_chat_toggle(void);

/* True while the text input field is open */
bool chat_is_typing(void);

#endif /* CHAT_H */
