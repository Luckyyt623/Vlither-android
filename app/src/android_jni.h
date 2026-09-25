#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

#include <stdbool.h>
#include <stddef.h>

long android_jni_get_unlock_remaining_ms(void);

void android_jni_request_ad(void);

void android_jni_notify_game_ready(void);

void android_jni_open_url(const char* url);
/* Opens `url` in an in-app WebView (TagsStoreActivity) with a close button,
   instead of leaving to the system browser like android_jni_open_url(). */
void android_jni_open_webview(const char* url);
bool android_jni_request_custom_arrow(void);
/* Opens the system image picker to choose a custom background image, used
   for both the homepage and in-game "Custom (Upload)" background option. */
bool android_jni_request_custom_background(void);
bool android_jni_request_settings_backup(void);
bool android_jni_request_settings_restore(void);
/* Decode an Android asset (including WebP) to malloc-owned RGBA bytes. */
unsigned char* android_jni_decode_asset_rgba(const char* asset_path,
                                             int* width, int* height);
/* Decode PNG/JPEG/WebP bytes to malloc-owned RGBA pixels. */
unsigned char* android_jni_decode_image_rgba(const unsigned char* encoded,
                                             size_t encoded_size,
                                             int* width, int* height);

/* Event reminders survive the game activity and are delivered by Android at
   the event start time. Returns true when scheduled immediately; Android 13+
   may first show its notification-permission dialog. */
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

/* Local gameplay capture. The native client supplies persisted choices and
   whether a real (non-preview) server session is active. Android handles the
   MediaProjection consent/foreground service and MediaStore output. */
void android_jni_capture_sync(bool recording_enabled,
                              bool screenshots_enabled,
                              bool session_active);
void android_jni_capture_confirmed_kill(void);

#endif
#endif
