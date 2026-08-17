#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

#include <stdbool.h>
#include <stddef.h>

long android_jni_get_unlock_remaining_ms(void);

void android_jni_request_ad(void);

void android_jni_notify_game_ready(void);

void android_jni_open_url(const char* url);

/* Decode an Android asset (including WebP) to malloc-owned RGBA bytes. */
unsigned char* android_jni_decode_asset_rgba(const char* asset_path,
                                             int* width, int* height);

/* Event reminders survive the game activity and are delivered by Android at
   the event start time. Returns true when scheduled immediately; Android 13+
   may first show its notification-permission dialog. */
bool android_jni_schedule_event_notification(const char* event_id,
                                             const char* event_name,
                                             const char* server_ip,
                                             long long start_at_ms);
void android_jni_cancel_event_notification(const char* event_id);

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

/* Vlither Voice Android bridge. PCM is signed 16-bit little-endian, mono,
   16 kHz. Capture packets are queued by Kotlin and drained by the native
   Mongoose/WebSocket loop on the render thread. */
bool android_jni_voice_poll_capture(unsigned char* out, size_t cap, size_t* out_len);
void android_jni_voice_prepare(void);
int android_jni_voice_audio_state(void);
void android_jni_voice_set_capture(bool active);
void android_jni_voice_play_pcm(const char* speaker_id, const unsigned char* pcm,
                                size_t len, float gain);
void android_jni_voice_stop_playback(void);

#endif
#endif
