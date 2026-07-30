#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

#include <stdbool.h>

long android_jni_get_unlock_remaining_ms(void);

void android_jni_request_ad(void);

void android_jni_notify_game_ready(void);

void android_jni_open_url(const char* url);

const char* android_jni_get_clipboard_text(void);

void android_jni_set_clipboard_text(const char* text);

void android_jni_set_text_input_active(bool active);
bool android_jni_enqueue_clipboard_paste(void);

typedef enum android_ime_event_type {
    ANDROID_IME_EVENT_NONE = 0,
    ANDROID_IME_EVENT_TEXT        = 1,
    ANDROID_IME_EVENT_KEY         = 2,
    ANDROID_IME_EVENT_COMPOSITION = 3,
} android_ime_event_type;

typedef struct android_ime_event {
    android_ime_event_type type;
    char* text;
    int keycode;
    int action;
    int meta_state;
    int replace_codepoints;
} android_ime_event;

bool android_jni_poll_ime_event(android_ime_event* out_event);
void android_jni_release_ime_event(android_ime_event* event);

#endif
#endif
