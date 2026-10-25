#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

#include <stdbool.h>
#include <stddef.h>



void android_jni_notify_game_ready(void);

void android_jni_open_url(const char* url);
void android_jni_open_webview(const char* url);
void android_jni_open_audio_settings(void);
int  android_jni_get_screen_orientation_mode(void);
bool android_jni_set_screen_orientation_mode(int mode);

bool android_jni_request_custom_arrow(void);
bool android_jni_request_profile_image(void);
bool android_jni_request_custom_background(void);
bool android_jni_request_settings_backup(void);
bool android_jni_request_settings_restore(void);
unsigned char* android_jni_decode_asset_rgba(const char* asset_path,
                                             int* width, int* height);
unsigned char* android_jni_decode_image_rgba(const unsigned char* encoded,
                                             size_t encoded_size,
                                             int* width, int* height);

bool android_jni_schedule_event_notification(const char* event_id,
                                             const char* event_name,
                                             const char* server_ip,
                                             long long start_at_ms);
void android_jni_cancel_event_notification(const char* event_id);
void android_jni_show_local_notification(const char* title,
                                         const char* body);

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

void android_jni_capture_sync(bool recording_enabled,
                              bool screenshots_enabled,
                              bool session_active);
void android_jni_capture_confirmed_kill(void);

/* Audio events -> GameActivity.onAudioEvent -> GameAudio (Kotlin) */
enum {
    AJNI_AUDIO_EAT = 0,        /* arg: 0..1000 food size factor */
    AJNI_AUDIO_DIE = 1,
    AJNI_AUDIO_KILL = 2,
    AJNI_AUDIO_GAME_START = 3,
    AJNI_AUDIO_GAME_STOP = 4,
    AJNI_AUDIO_BOOST_ON = 5,
    AJNI_AUDIO_BOOST_OFF = 6,
    AJNI_AUDIO_SNAKE_DEAD = 7, /* arg: 0..1000 snake length / max length */
};
void android_jni_audio_event(int ev, int arg);

#endif
#endif
