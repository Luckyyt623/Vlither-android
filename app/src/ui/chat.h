#ifndef CHAT_H
#define CHAT_H

#include <thermite.h>

void ui_chat_init(tenv* env);
void ui_chat(tenv* env);
void ui_chat_add_message(const char* sender, const char* text);
void ui_chat_destroy(tenv* env);
bool chat_is_typing(void);

#endif
