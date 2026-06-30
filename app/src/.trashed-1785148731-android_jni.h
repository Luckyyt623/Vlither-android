#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

/**
 * Returns ms remaining (>0) if unlocked, -1 if locked/expired.
 * Reads the unlock file written by MainActivity.kt — NO JNI needed.
 */
long android_jni_get_unlock_remaining_ms(void);

/**
 * Brings MainActivity to the foreground to show the rewarded ad.
 * Uses g_android_app (set by thermite's android_main) for JNI.
 */
void android_jni_request_ad(void);

/**
 * Signals to GameActivity that the first Vulkan frame has rendered.
 * GameActivity will fade out and remove the loading overlay.
 */
void android_jni_notify_game_ready(void);

#endif /* ANDROID */
#endif /* ANDROID_JNI_H */
