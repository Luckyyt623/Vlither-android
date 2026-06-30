#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

long android_jni_get_unlock_remaining_ms(void);

void android_jni_request_ad(void);

void android_jni_notify_game_ready(void);

void android_jni_open_url(const char* url);

#endif
#endif
